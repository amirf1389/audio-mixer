# VST3 hosting

`sdk/pluginterfaces` are the Steinberg VST 3 interface headers (v3.7.12), unchanged, under their dual licence
(`sdk/LICENSE.txt`: Steinberg VST3 License or GPLv3, the same regime as the ASIO SDK in `../asio/sdk`). Only the headers are
here: the host (`engine/cpp/app/vst3`) talks to plug-ins through these interfaces and implements the host-side objects itself,
so nothing from `public.sdk` or VSTGUI is compiled in. A mix OS loads third-party VST3 plug-ins; it is not badged as VST and does
not ship any. Read the licence before distributing a build.
