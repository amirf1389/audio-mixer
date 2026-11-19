# AmF X1 for live streaming with low processor use

This guide shows how to run AmF X1 beside a streaming program such as TikTok LIVE Studio or OBS Studio while it uses little of the processor. It applies to AmF X1 1.24.0.5 on Windows.

## What keeps the processor use low
- Channels with no input are skipped. A channel that carries only silence and has no audio effects inserted costs no processing time.
- The interface slows down by itself. It redraws 30 times a second while you work in it, 4 times a second when another program is in front, and once a second when it is minimised. The sound is never slowed down.
- ECO mode halves the redraw rate while the console is in front.
- The audio thread runs on the fast cores of the processor, and the interface is marked as background work when another program is in front.
- A larger buffer means fewer, larger blocks of work. Buffer sizes go from 32 to 8192 samples.

## Measured on our test machine

The console measures itself in its self-test. On our test machine (Linux, drawing without a graphics card, one processor core counted) the whole program used about 7 percent of one core in front, about 5 percent behind another program and about 4 percent minimised.

These figures are from that one machine. Your computer will give other figures. We have not compared it with other products. We have not yet measured it on Windows while TikTok LIVE Studio is running.

## Set up for a stream
- Open the TOOLS menu and choose LIVE MODE. The microphone channel gets a voice chain: noise reduction, automatic microphone equaliser, level rider and peak remover.
- The master is held 3 dB under full scale, so the stream does not clip.
- The watchdog is switched on. If the sound card stops, the console starts the sound again by itself.
- The window shows only the LIVE screen with the levels and a mute button. Leaving it needs the console PIN, so a guest cannot change the mix.
- To start LIVE MODE by itself, switch on AUTO LIVE in the TOOLS menu. When a streaming program opens, LIVE MODE starts.
- In the streaming program, choose the sound output of AmF X1 as the audio source.

## How to use the least processing power
- Minimise the console during the stream. It then redraws once a second.
- Use a buffer of 512 or 1024 samples. Use a small buffer only when you listen to yourself through the console.
- Keep effects only on the channels that need them.
- Close the pages with large moving displays, such as the analyser, before you minimise.

## Limits
- A large buffer adds delay. At 8192 samples and 48 kHz one block lasts about 171 milliseconds.
- Set your own PIN on the ACCESS page before you rely on the lock. The factory PIN is the same on every copy of the program, so it does not protect anything.
- The automatic microphone equaliser corrects the tone by measuring the voice. It is signal processing, not machine learning.
