# AmF X1 1.27 console layout

The 1.27 mixer keeps one functional structure and gives it its own proportions:

```
+------+-----------------------------------------------------------------+
| rail | top bar: title · LOCK · clock · scene LOAD/SAVE · network        |
|      +-------+-----------------------------------------+---------------+
| 76px | bank  | fader strips (scroll sideways)          | monitor desk  |
|      | 96 px | 124 px default (96-180, Ctrl+Shift +/-) | ~18 % width   |
+------+-------+-----------------------------------------+---------------+
```

The mute-group assignments have their own page (ASSIGN · MUTE GROUPS). It is not packed above the faders, so the strips keep their full height.

## Grid and sizes

- **8 px grid.** Margins, gaps and paddings on the new parts are 8 or 16 px.
- **Touch targets.** Every new key is at least 44 px high. The rail's section keys are 52 px, the group MUTE keys 64 px and the TALKBACK key 52 px.
- **Rail.** The rail is 76 px wide, as before. Eight section keys (HOME, MIX, EFFECTS, METERS, ROUTING, SETUP, MONITOR, ASSIGN) sit above the full page list. Each opens an existing page, so no page is duplicated. MONITOR opens the MIXER, where the monitor desk is.
- **Bank column.** 96 px wide, at the left of the faders. It has the layer keys (CH 1-16, CH 17-32, CH 33-40 when the console has them, ALL CH, AUX, BUSES, MAIN / MATRIX, DCA, ALL) and the strip modes EDIT and MOVE at the bottom.
- **Strips.** The strips are 124 px wide by default. Each one has:
  - a 32 px name tab in its own colour, with a kind icon (mic, instrument, FX or headphones; click it to change), the number and the name;
  - a 24 px dB readout box under the fader.
- **Monitor desk.** The right column takes `max(200, 18 % of the page)`, rounded to 8 px. Its sections go from top to bottom:
  - DIM, MONO, MUTE;
  - output A, B or A+B;
  - PHONES;
  - TALKBACK, with its level and destination;
  - CLR SOLO;
  - a master strip (MAIN LR, M1-M4 or B1-B16) with its own fader, meter, readout and COMP / INS lamps.
- **Top bar.** The header is 60 px high, with 44 px chips. They show when the window is at least 1400 px wide, and the words drop under 1800 px.

## Assign page

- **Group keys.** Eight group MUTE keys run across the top, four per row under 900 px. Each has the group's colour bar, its name, MUTE or MUTED, and an EDIT key that shows the SOURCES count.
- **Group editor.** It has:
  - a 240 px name field;
  - eight 44 px colour swatches;
  - collapsible sections (CHANNELS, AUX BUSES, FX BUSES, MAINS · MATRIX) with 64 × 44 px source cells.

  Only the sections that are open are built, which keeps the page inside the page-open budget even with 300 channels.
- **Mains and matrix.** These stay out of mute groups on purpose, so that one group key can never silence the whole show.

## Colour and type

All colours come from `Theme`: accent, cards and the strip colours. All type uses `Theme.mono`, from 9 px for labels up to 16 px for the page title. In the COCKPIT theme the keys switch to square, backlit push-keys with a lit bar, through `Theme.radiusS` and `Theme.cockpit`.
