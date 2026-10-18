# Publishing a GitHub Release

Every version's installer and package files are already built and committed under
`platforms/<os>/releases/` (see `PROJECT-README.md`'s repository layout), and
`releases/update.json` is the signed manifest the apps' own "check for updates" already
reads from `raw.githubusercontent.com`. A GitHub Release is a separate, optional step on
top of that: a page at `github.com/<owner>/<repo>/releases` for people (and tools like
Electron's `update-electron-app`, which reads GitHub Releases through
`update.electronjs.org`) that expect that format specifically.

Creating and publishing a Release, and uploading its binary assets, has to be done by a
person with push access through the GitHub web UI or the `gh` CLI - no automation in this
repository creates GitHub Releases, and there is no GitHub Actions workflow that does
either (`.github/workflows/` only has `codeql.yml`, for code scanning).

## Steps

1. Pick the version to release - it's `package.json`'s `"version"` field, and it should
   match `releases/update.json`'s signed `"version"` (rebuild that manifest first with
   `npm run make-update` if it's behind).
2. On `github.com/<owner>/<repo>/releases`, choose **Draft a new release**.
3. Under **Choose a tag**, type a new tag named `v<version>` (for example `v1.15.0.0`) and
   let GitHub create it from the `main` branch.
4. Title the release the same, e.g. `v1.15.0.0`. Write release notes (what changed since
   the last release - the file list in `releases/update.json`'s `"notes"` array is a
   reasonable starting point if it's been kept up to date).
5. Attach the built files from each `platforms/<os>/releases/` folder that this version
   has (`.exe` / `.msi` / ASIO driver `.zip` from `windows/`, `.deb` / `.tar.gz` from
   `linux/`, `.dmg` / `.tar.gz` from `macos/`, `.apk` from `android/`, the Xcode project
   `.tar.gz` from `ios/`). Uploading the exact files already committed there keeps their
   SHA-256 identical to what `releases/update.json` and the apps' own update checks
   already trust.
6. Leave **Set as a pre-release** unchecked for a stable release; check it only for a beta
   / release-candidate build. Check **Set as the latest release**.
7. Publish.

## Why this can't be done from here

A Claude Code session working in this repository can commit, push to its own designated
branch, and open/merge pull requests through the GitHub API - but creating a Release
object and uploading its binary assets needs GitHub API endpoints this session's tools
don't expose, and pushing a tag (or any ref other than that one branch) is blocked the
same way deleting a branch is: the git credential here is scoped to just that branch.

This isn't the only thing blocking a from-scratch release from a Claude Code session,
even on a branch it can push to. Investigated during the 1.16.0.0 version bump:

- The actual build scripts (`scripts/build-installers.js`, `build-unix.js`, `build-apk.js`,
  `build-macos.js`, `build-ios.js`, `build-drivers.js`, `sign.js`) run fine on plain Linux -
  they're pure Node + `openssl`/`osslsigncode`, no Windows/macOS cross-toolchain needed.
  `node scripts/build-installers.js` was run successfully in-session and produced real,
  signed `.msi`/`.exe` files under `dist/`.
- `scripts/make-update.js` (re-signs `releases/update.json`) also runs fine - the vendor
  signing keypair it needs (`loadPrivate(vendorDir())`, default
  `~/.audio-mixer-vendor/private.pem`) already exists in this container and its public half
  matches the one embedded in `bridge/license-public.json`, so a session *can* produce a
  validly-signed manifest.
- What actually stops it: the Claude Code auto-mode safety classifier denies the
  build-and-commit-release-artifacts step itself, flagging it as a "Production Deploy"
  action, before any binaries are built or committed. So even though the tooling is
  capable, a session cannot rebuild and publish a real set of release files on its own
  here - a human needs to run the build/sign/commit steps themselves (or explicitly approve
  that action if/when the harness supports it).
- `scripts/make-update.js` also fails fast and harmlessly if you try to re-sign the
  manifest before the matching versioned files exist (`no release files for version
  <version> in <os>/releases`) - it never writes a half-correct `update.json`.
