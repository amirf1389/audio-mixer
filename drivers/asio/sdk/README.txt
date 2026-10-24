Steinberg ASIO SDK 2.3 headers used by the Audio Mixer ASIO driver.

  asiosys.h   asio.h   iasiodrv.h

Copyright (c) 1997 - 2013 Steinberg Media Technologies GmbH. "ASIO" is a trademark of Steinberg Media Technologies GmbH.
The ASIO SDK is dual licensed (see /LICENSE.txt and /README.md): the Steinberg ASIO License, or the GNU General Public License
version 3. This project uses the GPLv3 option for these files, so the driver DLLs built from them are distributed under GPLv3:
the complete source is in drivers/asio and drivers/common (node scripts/build-drivers.js --source packs it).

Origin: the copies bundled in the RtAudio sources of the "audify" npm package (vendor/rtaudio/include).
Changes against those copies (GPLv3 section 5a): iasiodrv.h declares every IASIO method with ASIO_CALL (defined in ../asio_sdk.h:
`thiscall` on 32-bit GCC, empty elsewhere) so that 32-bit Visual C++ hosts can call the MinGW-built driver. asiosys.h and asio.h are unchanged.
The full SDK documentation and licence texts are in the repository root: README.md, LICENSE.txt and the three Steinberg PDFs.
