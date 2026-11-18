# Plug-in downloads

Put a plug-in file (`Name.vst3`) here and list it in `releases/plugins.json`:

```json
{ "plugins": [ { "id": "amfb-warm", "name": "AMFB Warm Channel", "version": "1.0", "note": "what it does, in one line",
    "files": { "win-x64-exe": "AMFB Warm Channel.vst3" } } ] }
```

Then run `node scripts/make-update.js` with your vendor key: the size and sha256 of each file go into the signed `releases/update.json`.
The console offers only what that signed list contains, downloads over https from this repository, checks size and sha256, and saves the
file into its own plug-in folder. A plug-in is program code: list only plug-ins you built or have the right to distribute.

The key under `files` is the platform key the console asks for (the same keys as the installers: `win-x64-exe`, `linux-deb` ...).
