# Contributing

## Layout

Each operating system has its own top-level folder with its source, drivers and release files
(`android/`, `ios/`, `windows/`, `macos/`, `linux/`); code shared by all of them lives in a few
top-level folders (`bridge/`, `client/`, `scripts/`, `engine/`, `drivers/`, ...). See
[PROJECT-README.md](../PROJECT-README.md#repository-layout) for the full map.
`scripts/layout.js` is the one place that knows this layout - the build scripts and the test
suite read it from there, so a change to the layout has to start in that file.

## Before sending a change

```
node --test bridge/test.js
```

This is the whole test suite (server, bridge modules, installers/build scripts, and the page's
own inline scripts, exercised through `node:vm`). It needs to pass before a pull request is
useful to review.

For a change to `index.html`, also check that every inline `<script>` block still parses:

```
node -e "const fs=require('fs');const re=/<script\b(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script>/g;const html=fs.readFileSync('index.html','utf8');let m;while((m=re.exec(html)))new (require('vm').Script)(m[1]);console.log('ok')"
```

## Pull requests

- Keep a change scoped to what it's about; a bug fix doesn't need a drive-by refactor next to it.
- Add or update a test in `bridge/test.js` for anything that can be exercised without real
  hardware (ASIO/WASAPI/Core Audio drivers, a VST host, an Android/iOS build) - most of this
  codebase can, including the installers, which are tested by running the real build functions
  against a fake Node.js/npm zip rather than skipped.
- If a change touches something genuinely untestable here (needs a Windows machine's real audio
  driver, an Apple signing identity, etc.), say so in the PR description and what you checked by
  hand instead.
