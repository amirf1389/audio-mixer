'use strict';
// 6. ANDROID: manifest flags (exported components, cleartext, backup, debuggable), the network security config, release signing,
// the self-update path (signed manifest + sha256 before PackageInstaller) and WebView use (JS bridges, file access, URL allow-lists).
const { matchAll } = require('../lib/core');

const CHECK = 'android';

function attr(tag, name) { const m = new RegExp('android:' + name + '\\s*=\\s*"([^"]*)"').exec(tag); return m ? m[1] : null; }

function manifests(ctx) {
  for (const rel of ctx.files.filter(f => /(^|\/)AndroidManifest\.xml$/.test(f) && !/\/(build|test|androidTest)\//.test(f))) {
    const t = ctx.read(rel) || '';
    const app = /<application\b[^>]*>/s.exec(t);
    const lineAt = i => t.slice(0, i).split('\n').length;
    if (app) {
      const L = lineAt(app.index);
      if (attr(app[0], 'debuggable') === 'true') ctx.add({ severity: 'HIGH', check: CHECK, id: 'debuggable', title: 'android:debuggable="true" in the manifest', file: rel, line: L });
      const backup = attr(app[0], 'allowBackup');
      if (backup === 'true') ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'allow-backup', title: 'android:allowBackup="true" (app data can be copied off the device)', file: rel, line: L });
      else if (backup === null && /<activity|<service/.test(t)) ctx.add({ severity: 'LOW', check: CHECK, id: 'allow-backup-default', title: 'android:allowBackup not set (defaults to true)', file: rel, line: L });
      if (attr(app[0], 'usesCleartextTraffic') === 'true') ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'cleartext', title: 'android:usesCleartextTraffic="true"', file: rel, line: L });
      if (!attr(app[0], 'networkSecurityConfig') && /INTERNET/.test(t)) ctx.add({ severity: 'LOW', check: CHECK, id: 'no-nsc', title: 'no networkSecurityConfig (platform defaults apply)', file: rel, line: L });
    }
    for (const h of matchAll(t, /<(activity|activity-alias|service|receiver|provider)\b([^>]*?)(\/>|>([\s\S]*?)<\/\1>)/)) {
      const tag = h.m[2], body = h.m[4] || '', kind = h.m[1], name = attr(tag, 'name') || '?';
      const exported = attr(tag, 'exported');
      const hasFilter = /<intent-filter/.test(body);
      const isLauncher = /android\.intent\.action\.MAIN/.test(body) && /category\.LAUNCHER/.test(body);
      const perm = attr(tag, 'permission');
      if (kind === 'provider' && exported === 'true' && !perm && !attr(tag, 'readPermission')) ctx.add({ severity: 'HIGH', check: CHECK, id: 'exported-provider', title: 'exported content provider without a permission: ' + name, file: rel, line: h.line });
      if (exported === 'true' && !isLauncher && !perm && kind !== 'provider') {
        const lineText = t.split('\n')[h.line - 1] || '';
        const explained = /<!--[^>]*exported/.test(lineText);
        ctx.add({ severity: kind === 'activity' || kind === 'activity-alias' ? (explained ? 'LOW' : 'MEDIUM') : 'MEDIUM', check: CHECK, id: 'exported-component', title: `exported ${kind} without a permission: ${name}` + (explained ? ' (reason given in the manifest; it must treat every Intent extra as untrusted)' : ''), file: rel, line: h.line });
      }
      if (exported === null && hasFilter) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'implicit-export', title: `${kind} ${name} has an intent-filter but no android:exported (implicitly exported before Android 12)`, file: rel, line: h.line });
    }
    for (const h of matchAll(t, /uses-permission[^>]*android:name="android\.permission\.(REQUEST_INSTALL_PACKAGES|WRITE_SETTINGS|SYSTEM_ALERT_WINDOW|READ_SMS|READ_CALL_LOG|MANAGE_EXTERNAL_STORAGE|QUERY_ALL_PACKAGES)"/)) {
      ctx.add({ severity: h.m[1] === 'REQUEST_INSTALL_PACKAGES' ? 'INFO' : 'LOW', check: CHECK, id: 'sensitive-permission', title: 'sensitive permission: ' + h.m[1], file: rel, line: h.line });
    }
  }
}

function networkConfig(ctx) {
  for (const rel of ctx.files.filter(f => /res\/xml\/network_security_config[^/]*\.xml$/.test(f))) {
    const t = ctx.read(rel) || '';
    const base = /<base-config\b[^>]*>/.exec(t);
    if (base && /cleartextTrafficPermitted="true"/.test(base[0])) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'nsc-cleartext-base', title: 'network security config allows clear-text HTTP to every host by default (only public hosts listed in domain-config are HTTPS-only)', file: rel, line: t.slice(0, base.index).split('\n').length, detail: 'the LAN remote needs http to private addresses; make sure every internet endpoint (updates, licences, streams) is in the HTTPS-only domain-config' });
    for (const h of matchAll(t, /<certificates\s+src="user"/)) {
      const inDebug = /<debug-overrides>[\s\S]*$/.test(t.slice(0, h.index)) && !/<\/debug-overrides>/.test(t.slice(t.lastIndexOf('<debug-overrides>', h.index), h.index));
      ctx.add({ severity: inDebug ? 'INFO' : 'MEDIUM', check: CHECK, id: 'nsc-user-ca', title: 'user-installed CAs trusted' + (inDebug ? ' (debug builds only)' : ''), file: rel, line: h.line });
    }
    if (/<pin-set/.test(t)) ctx.add({ severity: 'INFO', check: CHECK, id: 'nsc-pins', title: 'certificate pins configured', file: rel });
  }
}

function gradle(ctx) {
  for (const rel of ctx.files.filter(f => /^android\/.*build\.gradle(\.kts)?$/.test(f))) {
    const t = ctx.read(rel) || '';
    const rel_ = /release\s*\{([^}]*)\}/.exec(t);
    if (rel_) {
      const L = t.slice(0, rel_.index).split('\n').length;
      if (/isDebuggable\s*=\s*true|debuggable\s+true/.test(rel_[1])) ctx.add({ severity: 'HIGH', check: CHECK, id: 'release-debuggable', title: 'release build type is debuggable', file: rel, line: L });
      if (/signingConfigs\.getByName\(\s*"debug"\s*\)/.test(rel_[1])) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'release-debug-signing', title: 'release build falls back to the debug signing key when no release keystore is configured', file: rel, line: L, detail: 'fail the release build instead: an APK signed with the public debug key can be replaced by anyone' });
      if (/isMinifyEnabled\s*=\s*false/.test(rel_[1])) ctx.add({ severity: 'INFO', check: CHECK, id: 'no-minify', title: 'release build is not minified / obfuscated', file: rel, line: L });
    }
    for (const h of matchAll(t, /(storePassword|keyPassword)\s*=\s*"([^"$]{4,})"/)) ctx.add({ severity: 'HIGH', check: CHECK, id: 'gradle-keystore-password', title: 'keystore password written into the build file', file: rel, line: h.line });
    for (const h of matchAll(t, /usesCleartextTraffic\s*=\s*true|manifestPlaceholders[^\n]*cleartext[^\n]*true/i)) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'gradle-cleartext', title: 'clear-text traffic switched on from Gradle', file: rel, line: h.line });
  }
}

function code(ctx) {
  const src = ctx.files.filter(f => /^android\/.*\/src\/main\/.*\.(kt|java)$/.test(f));
  for (const rel of src) {
    const t = ctx.read(rel) || '';
    // self-update: PackageInstaller only after a verified signature and hash
    if (/PackageInstaller\.Session|PackageInstaller\.SessionParams|ACTION_INSTALL_PACKAGE|application\/vnd\.android\.package-archive/.test(t)) {
      const sha = /sha256|SHA-256|MessageDigest/.test(t), sig = /verif\w*\(|Signature\.getInstance|SHA256withECDSA|verifiedManifest/.test(t);
      if (!sha || !sig) ctx.add({ severity: 'HIGH', check: CHECK, id: 'install-unverified', title: 'APK install path without ' + [!sig && 'signature', !sha && 'sha256'].filter(Boolean).join(' and ') + ' verification', file: rel });
      else ctx.add({ severity: 'INFO', check: CHECK, id: 'install-verified', title: 'APK install: signed manifest and sha256 checked before PackageInstaller', file: rel });
      if (/"http:\/\//.test(t) && !/https only|"https" ->/.test(t)) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'install-http', title: 'APK / manifest may be fetched over http', file: rel });
    }
    // WebView
    for (const h of matchAll(t, /addJavascriptInterface\s*\(/)) ctx.add({ severity: /loadUrl\(\s*"file:\/\/\/android_asset/.test(t) && !/loadUrl\(\s*(url|u|link|e\.)/.test(t) ? 'MEDIUM' : 'HIGH', check: CHECK, id: 'webview-js-bridge', title: 'WebView JavaScript bridge (addJavascriptInterface): any page loaded in it can call native code', file: rel, line: h.line });
    for (const h of matchAll(t, /setAllowUniversalAccessFromFileURLs\(\s*true|setAllowFileAccessFromFileURLs\(\s*true|allowUniversalAccessFromFileURLs\s*=\s*true|allowFileAccessFromFileURLs\s*=\s*true/)) ctx.add({ severity: 'HIGH', check: CHECK, id: 'webview-file-urls', title: 'WebView lets file:// pages read other files / origins', file: rel, line: h.line });
    for (const h of matchAll(t, /setAllowFileAccess\(\s*true|allowFileAccess\s*=\s*true/)) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'webview-file-access', title: 'WebView file access on', file: rel, line: h.line });
    for (const h of matchAll(t, /onReceivedSslError[\s\S]{0,200}?\.proceed\(\)/)) ctx.add({ severity: 'HIGH', check: CHECK, id: 'webview-ssl-proceed', title: 'WebView ignores TLS errors (handler.proceed())', file: rel, line: h.line });
    for (const h of matchAll(t, /javaScriptEnabled\s*=\s*true|setJavaScriptEnabled\(\s*true/)) {
      const guarded = /shouldOverrideUrlLoading[\s\S]{0,300}?(allowed|allow|host|startsWith|whitelist|allowList)/.test(t);
      ctx.add({ severity: guarded ? 'INFO' : 'MEDIUM', check: CHECK, id: 'webview-js', title: 'WebView with JavaScript' + (guarded ? ', navigation limited by an allow-list' : ' and no navigation allow-list'), file: rel, line: h.line });
    }
    for (const h of matchAll(t, /MODE_WORLD_(READABLE|WRITEABLE)/)) ctx.add({ severity: 'HIGH', check: CHECK, id: 'world-readable', title: 'world-readable / writeable file mode', file: rel, line: h.line });
    for (const h of matchAll(t, /Log\.[dvi]\([^)]*(token|password|secret|licen[cs]e\s*key)/i)) ctx.add({ severity: 'MEDIUM', check: CHECK, id: 'log-secret', title: 'secret-looking value written to logcat', file: rel, line: h.line });
    for (const h of matchAll(t, /TrustManager|HostnameVerifier\s*\{|ALLOW_ALL/)) ctx.add({ severity: 'HIGH', check: CHECK, id: 'custom-trust', title: 'custom TLS trust / hostname verification', file: rel, line: h.line });
  }
  ctx.note(CHECK, `${src.length} Android sources checked`);
}

function run(ctx) { manifests(ctx); networkConfig(ctx); gradle(ctx); code(ctx); }

module.exports = { run, CHECK };
