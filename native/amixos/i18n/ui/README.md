# UI string tables

`ui-keys.json` (one folder up) lists every interface string the QML pages wrap in `i18n.t("...", i18n.lang)`: the English text is the key.
Each `<lang>.json` here maps those keys to one language: `{"English key": "translation", ...}`. Rules:

- Keep leading / trailing spaces, `·`, `•`, `(`, `:` and `\n` exactly as in the key: many keys are fragments glued to numbers or names
  (" Hz · ", "ENGINE RUNNING on ", " (free). Have a key? Paste it:").
- Keep product and protocol names: AmF X1, AMFB Audio, A mix OS, ASIO, WASAPI, DirectSound, Core Audio, ALSA, VST, VST2, VST3, OSC, Bluetooth,
  A2DP, HFP, WAV, CSV, JSON, LUFS, dBFS, dBTP, EBU R128, PIN, DCA, AUX, FX, EQ, RTA, FFT, DSP, LAN, UDP, IP, OTA, Realtek, Steinberg, TouchOSC,
  Mixing Station, Windows, YouTube, Spotify, Apple Music, TIDAL, Deezer, Roon, Qobuz, Amazon Music, file paths (~/.amixos/...), key names (Esc, Del).
- Audio vocabulary: "wet" = effect signal, "strip" = channel strip, "fader", "bus", "send", "insert", "gate", "compressor", "limiter", "clip" (overload),
  "frames" = buffer samples, "block" = buffer, "ladder" = fallback order of drivers, "arm" = arm for recording, "pan", "mute", "solo", "bypass".
- Page names that appear INSIDE a sentence in capitals (CONNECTIONS, SETTINGS, MIXER, ROUTING, LICENSE, ACCOUNT, DIAGNOSTICS, CALIBRATE, SCENES,
  PRESETS, LEVELS, LOUDNESS, SEND TO, LIVE SOURCES, MIC EQ, TONE (EQ), AI MIX, SOUND CARD, ARRANGE, STREAM & MEDIA, PLUGINS, NETWORK, HELP, ACCESS,
  ADMIN) use the same word the page rail uses: `../verified-22.json` holds them per language (reuse those words, in capitals).
- Keep the length close to the English: these are buttons and labels; capitals stay capitals where the script has them.
- Machine translations: the language menu marks every language but English as "auto" for these strings until a native speaker checks the file.
