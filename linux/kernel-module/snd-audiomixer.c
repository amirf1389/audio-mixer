// SPDX-License-Identifier: GPL-2.0
/* snd-audiomixer: a virtual ALSA sound card "Audio Mixer" in the Linux kernel (stereo, signed 16-bit little-endian, 48 kHz, 20 ms periods).
 *
 *   an application plays to the card ──▶ play FIFO ──▶ /dev/audiomixer (read)   ──▶ am-pump ──▶ the mixer (LIVE SOURCES → READ)
 *   an application records from it  ◀── capture FIFO ◀── /dev/audiomixer (write) ◀── am-pump ◀── the mixer (LIVE SOURCES → WRITE)
 *
 * The card keeps its own clock (a high-resolution timer), so applications never block when the mixer is not running: the played audio is dropped
 * and the recorded audio is silence. Only plain read / write / poll are offered (no mmap). Written against the kernel sound API of Linux 5.10 to 6.x;
 * it was NOT compiled or loaded on a real kernel where this file was written (no kernel headers there): build it with `make` on the target machine. */
#include <linux/module.h>
#include <linux/version.h>
#include <linux/platform_device.h>
#include <linux/hrtimer.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>

#define AM_RATE 48000
#define AM_CHANNELS 2
#define AM_FRAME_BYTES (AM_CHANNELS * 2)
#define AM_PERIOD_FRAMES 960                                    /* 20 ms */
#define AM_PERIOD_BYTES (AM_PERIOD_FRAMES * AM_FRAME_BYTES)
#define AM_PERIODS_MAX 8
#define AM_FIFO_BYTES 65536                                     /* about 340 ms; a power of two */

struct am_stream {
	struct hrtimer timer;
	struct snd_pcm_substream *substream;
	ktime_t period;
	unsigned int pos;                                           /* bytes, inside the ring buffer of the substream */
	bool running;
};

static struct platform_device *am_pdev;
static struct snd_card *am_card;
static DEFINE_KFIFO(am_play_fifo, u8, AM_FIFO_BYTES);           /* played by applications; single producer: the timer, single consumer: read() */
static DEFINE_KFIFO(am_cap_fifo, u8, AM_FIFO_BYTES);            /* for applications that record; single producer: write() (am_write_lock), consumer: the timer */
static DEFINE_MUTEX(am_write_lock);
static DECLARE_WAIT_QUEUE_HEAD(am_read_wq);
static DECLARE_WAIT_QUEUE_HEAD(am_write_wq);

static const struct snd_pcm_hardware am_hw = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = AM_RATE, .rate_max = AM_RATE,
	.channels_min = AM_CHANNELS, .channels_max = AM_CHANNELS,
	.buffer_bytes_max = AM_PERIOD_BYTES * AM_PERIODS_MAX,
	.period_bytes_min = AM_PERIOD_BYTES, .period_bytes_max = AM_PERIOD_BYTES,
	.periods_min = 2, .periods_max = AM_PERIODS_MAX,
};

static enum hrtimer_restart am_tick(struct hrtimer *t)
{
	struct am_stream *s = container_of(t, struct am_stream, timer);
	struct snd_pcm_substream *ss = s->substream;
	struct snd_pcm_runtime *rt = ss->runtime;
	unsigned int buf = snd_pcm_lib_buffer_bytes(ss);

	if (!s->running)
		return HRTIMER_NORESTART;
	hrtimer_forward_now(t, s->period);
	if (ss->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (kfifo_avail(&am_play_fifo) >= AM_PERIOD_BYTES) {   /* whole periods only; when nobody reads, the audio is dropped */
			kfifo_in(&am_play_fifo, rt->dma_area + s->pos, AM_PERIOD_BYTES);
			wake_up_interruptible(&am_read_wq);
		}
	} else {
		unsigned int got = kfifo_out(&am_cap_fifo, rt->dma_area + s->pos, AM_PERIOD_BYTES);

		if (got < AM_PERIOD_BYTES)
			memset(rt->dma_area + s->pos + got, 0, AM_PERIOD_BYTES - got);   /* silence while the mixer sends nothing */
		wake_up_interruptible(&am_write_wq);
	}
	s->pos = (s->pos + AM_PERIOD_BYTES) % buf;
	snd_pcm_period_elapsed(ss);
	return HRTIMER_RESTART;
}

static int am_open(struct snd_pcm_substream *ss)
{
	struct am_stream *s = kzalloc(sizeof(*s), GFP_KERNEL);
	int err;

	if (!s)
		return -ENOMEM;
	s->substream = ss;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
	hrtimer_setup(&s->timer, am_tick, CLOCK_MONOTONIC, HRTIMER_MODE_REL_SOFT);
#else
	hrtimer_init(&s->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_SOFT);
	s->timer.function = am_tick;
#endif
	ss->runtime->private_data = s;
	ss->runtime->hw = am_hw;
	err = snd_pcm_hw_constraint_integer(ss->runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0) {
		kfree(s);
		return err;
	}
	return 0;
}

static int am_close(struct snd_pcm_substream *ss)
{
	struct am_stream *s = ss->runtime->private_data;

	s->running = false;
	hrtimer_cancel(&s->timer);
	kfree(s);
	return 0;
}

static int am_prepare(struct snd_pcm_substream *ss)
{
	struct am_stream *s = ss->runtime->private_data;

	s->pos = 0;
	s->period = ns_to_ktime(div_u64((u64)NSEC_PER_SEC * ss->runtime->period_size, ss->runtime->rate));
	return 0;
}

static int am_trigger(struct snd_pcm_substream *ss, int cmd)
{
	struct am_stream *s = ss->runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		s->running = true;
		hrtimer_start(&s->timer, s->period, HRTIMER_MODE_REL_SOFT);
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		s->running = false;
		hrtimer_try_to_cancel(&s->timer);
		return 0;
	}
	return -EINVAL;
}

static snd_pcm_uframes_t am_pointer(struct snd_pcm_substream *ss)
{
	struct am_stream *s = ss->runtime->private_data;

	return bytes_to_frames(ss->runtime, s->pos);
}

static const struct snd_pcm_ops am_ops = {
	.open = am_open, .close = am_close, .prepare = am_prepare, .trigger = am_trigger, .pointer = am_pointer,
};

/* ── /dev/audiomixer: the audio the card plays comes out of read(), the audio it records goes into write() ── */
static ssize_t am_dev_read(struct file *f, char __user *buf, size_t n, loff_t *off)
{
	unsigned int copied = 0;
	int err;

	if (kfifo_is_empty(&am_play_fifo)) {
		if (f->f_flags & O_NONBLOCK)
			return -EAGAIN;
		err = wait_event_interruptible(am_read_wq, !kfifo_is_empty(&am_play_fifo));
		if (err)
			return err;
	}
	err = kfifo_to_user(&am_play_fifo, buf, n, &copied);
	return err ? err : copied;
}

static ssize_t am_dev_write(struct file *f, const char __user *buf, size_t n, loff_t *off)
{
	unsigned int copied = 0;
	int err;

	if (kfifo_is_full(&am_cap_fifo)) {
		if (f->f_flags & O_NONBLOCK)
			return -EAGAIN;
		err = wait_event_interruptible(am_write_wq, !kfifo_is_full(&am_cap_fifo));
		if (err)
			return err;
	}
	if (mutex_lock_interruptible(&am_write_lock))
		return -ERESTARTSYS;
	err = kfifo_from_user(&am_cap_fifo, buf, n, &copied);
	mutex_unlock(&am_write_lock);
	return err ? err : copied;
}

static __poll_t am_dev_poll(struct file *f, poll_table *wait)
{
	__poll_t mask = 0;

	poll_wait(f, &am_read_wq, wait);
	poll_wait(f, &am_write_wq, wait);
	if (!kfifo_is_empty(&am_play_fifo))
		mask |= EPOLLIN | EPOLLRDNORM;
	if (!kfifo_is_full(&am_cap_fifo))
		mask |= EPOLLOUT | EPOLLWRNORM;
	return mask;
}

static const struct file_operations am_fops = {
	.owner = THIS_MODULE, .read = am_dev_read, .write = am_dev_write, .poll = am_dev_poll, .llseek = no_llseek,
};

static struct miscdevice am_misc = { .minor = MISC_DYNAMIC_MINOR, .name = "audiomixer", .fops = &am_fops, .mode = 0660 };

static int __init am_init(void)
{
	struct snd_pcm *pcm;
	int err;

	am_pdev = platform_device_register_simple("snd-audiomixer", -1, NULL, 0);
	if (IS_ERR(am_pdev))
		return PTR_ERR(am_pdev);
	err = snd_card_new(&am_pdev->dev, SNDRV_DEFAULT_IDX1, "AudioMixer", THIS_MODULE, 0, &am_card);
	if (err < 0)
		goto fail_dev;
	strscpy(am_card->driver, "snd-audiomixer", sizeof(am_card->driver));
	strscpy(am_card->shortname, "Audio Mixer", sizeof(am_card->shortname));
	strscpy(am_card->longname, "Audio Mixer virtual sound card", sizeof(am_card->longname));
	err = snd_pcm_new(am_card, "Audio Mixer", 0, 1, 1, &pcm);
	if (err < 0)
		goto fail_card;
	strscpy(pcm->name, "Audio Mixer", sizeof(pcm->name));
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &am_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &am_ops);
	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_VMALLOC, NULL, 0, AM_PERIOD_BYTES * AM_PERIODS_MAX);
	err = snd_card_register(am_card);
	if (err < 0)
		goto fail_card;
	err = misc_register(&am_misc);
	if (err < 0)
		goto fail_card;
	return 0;
fail_card:
	snd_card_free(am_card);
fail_dev:
	platform_device_unregister(am_pdev);
	return err;
}

static void __exit am_exit(void)
{
	misc_deregister(&am_misc);
	snd_card_free(am_card);
	platform_device_unregister(am_pdev);
}

module_init(am_init);
module_exit(am_exit);
MODULE_DESCRIPTION("Audio Mixer virtual sound card");
MODULE_AUTHOR("Audio Mixer");
MODULE_LICENSE("GPL");
