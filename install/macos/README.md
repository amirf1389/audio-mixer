# AmF X1 on macOS: where the files go

- **Program**: `/Applications/AmF X1/AmF X1.app` (drag the folder from the DMG). The engine, daemon and `libamix.dylib` sit inside the bundle (`Contents/MacOS`, `Contents/Frameworks`).
- **Data**: `/Applications/AmF X1/AmF X1 Data` when that folder is writable (the program keeps its data next to itself, as asked), otherwise `~/Library/Application Support/AmF X1`. Licence, console, scenes, presets, arrangement, audit log, key request.
- **Cache**: `~/Library/Caches/AmF X1` (rendered samples, VST scan, downloads, `prefetch/`). Safe to delete.
- **Temp**: `$TMPDIR/amf-x1`.

`settings.folders` on DIAGNOSTICS and `amix_paths_json()` in the SDK report what a running copy resolved. Overrides: `AMIX_DATA_DIR`, `AMIX_CACHE_DIR`, `AMIX_TEMP_DIR`.
