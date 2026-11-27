# Android wireframes (AmF X1 and AmF X1 Mixer)

The structure of both Android apps after the 1.26 build-3 redesign. One look everywhere: Material 3 dark in the AmF X1 colours
(`res/values/themes.xml`: warm primary `#FFB590`, cyan tertiary `#7FD8E8`, tonal surfaces, 16dp cards). Every key and tab is at
least 48dp tall. Parts switched off in SETTINGS → FEATURES (`Features.kt`) disappear from the bar, HOME and TOOLS; nothing is unlocked by it.

## Bottom navigation

```
AmF X1 Mixer (standalone)                    AmF X1 (full remote)
+------+------+-------+---------+--------+   +------+------+------+-------+--------+
| MIX  | PLAY | TOOLS | DEVICES |SETTINGS|   | HOME | MIX  | PLAY | TOOLS |SETTINGS|
+------+------+-------+---------+--------+   +------+------+------+-------+--------+
```

* PLAY follows the `streaming` feature, TOOLS `tools`, DEVICES `devices`. MIX, HOME and SETTINGS always stay.
* In AmF X1, DEVICES stays a HOME tile; MIX opens the mode used last (THIS PHONE / CONSOLE).
* On its side the bar shrinks to icons only.

## Screen frame

```
+----------------------------------------+
| status bar inset                       |
|  PAGE TITLE                            |
|  content (scrolls)                     |
|                                        |
+----------------------------------------+
| > Song · Artist · Spotify         [||] |  <- now-playing bar (only while a phone app plays)
| MASTER -12.0 dB          [-] [+] [M]   |  <- quick master (not on the phone mixer)
| MIX   PLAY   TOOLS   DEVICES  SETTINGS |
+----------------------------------------+
```

## MIX (compact channel strips)

```
+----------------------------------------+
| [ THIS PHONE | CONSOLE ]               |  (AmF X1 only)
| +----+----+----+----+   +------+       |
| |CH1 |CH2 |CH3 |CH4 |...|MASTER|       |
| | M  | M  | M  | M  |   |  M   |       |
| | S  | S  | S  | S  |   |      |       |
| |(o) |(o) |(o) |(o) |   | pan  |       |  pan knob
| | || | || | || | || |   |  ||  |       |  fader >= 30% of the strip
| | #  | #  | #  | #  |   |  ##  |       |  meter beside the fader
| |-6.0|0.0 |-inf|-3.0|   | 0.0  |       |
| +----+----+----+----+   +------+       |
|  <- swipe for more strips ->           |
+----------------------------------------+
```
Strips are sized from the screen width (MixerFit): 3+ across on a 360dp phone, 4+ on a tablet, 5-6+ on its side.

## PLAY (media)

```
+----------------------------------------+
| PLAY                                   |
| Control the music playing on this phone|
| +------------------------------------+ |
| | Allow media control                | |  only until Notification access is on;
| | why it is needed / what is not done| |  explains exactly what is read
| | [ Open Notification access ]       | |
| +------------------------------------+ |
| +------------------------------------+ |
| | +------+  Song title               | |
| | | art  |  Artist · Album           | |
| | +------+  Spotify                  | |
| | ===========o--------------         | |  seek (when the app allows it)
| |                        1:12 / 3:40 | |
| |      [ |< ]  [  ||  ]  [ >| ]      | |
| +------------------------------------+ |
| [TIDAL] [Spotify] ... sessions chooser |  (when more than one app has a session)
| Open a music app                       |
| [ TIDAL ] [ Spotify ] [ Deezer ]       |  text names, their own launch intents
| +------------------------------------+ |
| | On the computer                    | |  AmF X1 only, when the console's
| | Song · Artist · TIDAL              | |  STREAMING page reports a player
| | [ |< ] [ >|| ] [ >| ]              | |
| +------------------------------------+ |
| [ Web players ]                        |  the earlier PLAYERS page
| Remote control only: nothing recorded. |
+----------------------------------------+
```

## TOOLS / DEVICES / SETTINGS

```
TOOLS                    DEVICES                    SETTINGS
+------------------+     +-------------------+      +--------------------+
| [tile] [tile]    |     | consoles found    |      | connection         |
| [tile] [tile]    |     | [ CONSOLE A  > ]  |      | FEATURES  [x] ...  |
| recorder, pads,  |     | phone in / out    |      | text size, updates |
| calculator ...   |     | [ refresh ]       |      | about              |
+------------------+     +-------------------+      +--------------------+
```
Every earlier page stays reachable: from HOME tiles, TOOLS, the console's TOOLS list or PLAY (web players).
