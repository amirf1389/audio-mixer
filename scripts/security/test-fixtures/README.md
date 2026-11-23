Fixture tree for `scripts/security/test/audit.test.js`. Every file here is deliberately unsafe (or deliberately safe, `server/safe.js`).
Files end in `.fx` so no build, linter or the real audit treats them as sources; the test copies them to a temporary folder without the
suffix, adds the planted secrets it generates at run time (a fresh private key, an AWS-style key id, a `.env` file), and runs the audit
there. The real audit skips `scripts/security/test-fixtures/` entirely.
