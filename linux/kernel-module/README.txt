snd-audiomixer: a virtual ALSA sound card "Audio Mixer" as a Linux kernel module (stereo, signed 16-bit little-endian, 48 kHz).

  make                       builds snd-audiomixer.ko (needs linux-headers-$(uname -r) / kernel-devel) and am-pump (needs only a C compiler)
  sudo make install          installs both (depmod runs)
  sudo modprobe snd-audiomixer && am-pump
  or with DKMS:              sudo dkms add . && sudo dkms install audio-mixer/1.10.2     (rebuilds for every new kernel)

The card then shows up in aplay -l / arecord -l / PipeWire / PulseAudio as "Audio Mixer". What applications play to it is the mixer's input source
(LIVE SOURCES → READ on "Audio Mixer"); what the mixer sends to the device (WRITE) is what applications record from it. The card has its own clock, so
nothing blocks while the mixer is not running: played audio is dropped, recorded audio is silence. /dev/audiomixer is mode 0660, root only until you add a udev rule.

Status: am-pump is built and tested against the real Audio Mixer server. The kernel module was written against the sound API of Linux 5.10 to 6.x
and compiles without warnings (W=1) against the kernel headers of Linux 6.8, 6.14, 6.17 and 7.0, but it was NOT loaded or run on a real kernel. Unload with: sudo rmmod snd-audiomixer.
