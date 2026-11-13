# Third-party audio decoders

## FFmpeg (shipped)
- Version: FFmpeg **n7.1**, from https://git.ffmpeg.org/ffmpeg.git (tag `n7.1`).
- Licence: **LGPL 2.1 or later**. Built with `scripts/third_party/build-ffmpeg.sh`: decoders and demuxers for audio only,
  no `--enable-gpl`, no `--enable-nonfree` (the script stops if either slips in).
- How AmF X1 uses it: four separate DLLs next to `amixos.exe` (`avutil-59`, `swresample-5`, `avcodec-61`, `avformat-61`), opened at run
  time and never linked in. The program runs without them, and a user may replace them with their own build. `licenses\` in the install
  folder holds the LGPL text, the exact configure line and where the source is.
- Order on Windows: Windows' own decoders (Media Foundation) first, FFmpeg for what they cannot open (OGG, Opus, APE, WavPack, TTA, DSD,
  AC-3 / E-AC-3, DTS, TrueHD, AMR, CAF ...).
- Patents: some formats (AAC, AC-3, DTS ...) may carry patent duties in some countries; that is the seller's responsibility, not the
  LGPL's.

## MPEG-H 3D Audio (optional, NOT shipped)
- Fraunhofer FDK MPEG-H Software, **mpeghdec r4.0.1**, https://github.com/Fraunhofer-IIS/mpeghdec, wrapped by
  `engine/cpp/app/ext/mpegh/amfb_mpegh.cpp` into `amfb_mpegh.dll` (`scripts/third_party/build-mpeghdec.sh`).
- Its licence allows copying without fee but **commercial products need MPEG-H patent licences** (Via LA, www.via-la.com), and the
  source must be offered free of charge.
- It goes into the installer only with `AMIX_WITH_MPEGH=1` on `scripts/build-console-windows.sh`, once those licences are held. Without
  it, an MPEG-H file says: "the MPEG-H extension is not installed".
- Tested with Fraunhofer's MPEG-H test content (CICP 2 stereo and CICP 5, MHM1 in MP4): decoded and rendered to stereo, full length.
