'use strict';
// Minimal SMTP client (zero dependencies): enough to send a short plain-text notification through any mail server. Every transactional email
// provider (Gmail, Office 365, Amazon SES, Mailgun, Postmark, a company's own relay, ...) speaks SMTP, so this needs no vendor SDK or proprietary
// API key format: it is "an email API integration" in the universal sense, not locked to one vendor's HTTP API.
//   SMTP_HOST            required to send anything; send() resolves { sent: false, reason: 'not configured' } instead of throwing when it is unset,
//                         so a caller that emails as a best-effort alert never has to treat a missing mail server as a hard failure.
//   SMTP_PORT             default 587 (STARTTLS); 465 is treated as implicit TLS automatically; 25 / other ports try STARTTLS, then plain if the
//                         server does not offer it
//   SMTP_SECURE=1         force implicit TLS regardless of port
//   SMTP_USER, SMTP_PASS   AUTH LOGIN credentials (RFC 4954); omit both to send unauthenticated (an internal relay with no auth)
//   SMTP_FROM              the From: address; defaults to SMTP_USER
const net = require('node:net');
const tls = require('node:tls');
const crypto = require('node:crypto');

function configured(env = process.env) { return !!env.SMTP_HOST; }
function fromEnv(env = process.env) {
  return { host: env.SMTP_HOST, port: Number(env.SMTP_PORT) || 587, secure: env.SMTP_SECURE === '1' || Number(env.SMTP_PORT) === 465, user: env.SMTP_USER || '', pass: env.SMTP_PASS || '', from: env.SMTP_FROM || env.SMTP_USER || '' };
}

const EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
const b64 = s => Buffer.from(String(s), 'utf8').toString('base64');

// Reads SMTP reply lines (possibly several, "250-..." continuations ending in one "250 ..." final line) off a socket, resolving once the final
// line of one reply arrives. Multiple command()s in a row each get their own call: SMTP is strictly request/response, one reply per command.
function readReply(sock, timeoutMs) {
  return new Promise((resolve, reject) => {
    let buf = '';
    const timer = timeoutMs ? setTimeout(() => { cleanup(); reject(new Error('SMTP timed out waiting for a reply')); }, timeoutMs) : null;
    const onData = chunk => {
      buf += chunk.toString('latin1');
      const lines = buf.split('\r\n');
      const last = lines[lines.length - (buf.endsWith('\r\n') ? 2 : 1)];       // the last COMPLETE line
      if (last && /^\d{3} /.test(last)) { cleanup(); resolve({ code: Number(last.slice(0, 3)), text: lines.filter(Boolean).map(l => l.slice(4)).join('\n') }); }
    };
    const onErr = e => { cleanup(); reject(e); };
    const onClose = () => { cleanup(); reject(new Error('the mail server closed the connection')); };
    function cleanup() { if (timer) clearTimeout(timer); sock.removeListener('data', onData); sock.removeListener('error', onErr); sock.removeListener('close', onClose); }
    sock.on('data', onData); sock.on('error', onErr); sock.on('close', onClose);
  });
}
async function command(sock, line, ok, timeoutMs) {
  if (line !== null) sock.write(line + '\r\n');
  const r = await readReply(sock, timeoutMs);
  if (!ok(r.code)) throw new Error('the mail server refused it (SMTP ' + r.code + (r.text ? ': ' + r.text.split('\n')[0] : '') + ')');
  return r;
}
const is2xx = c => c >= 200 && c < 300;
const is3xx = c => c >= 300 && c < 400;

// Dot-stuffing (RFC 5321 4.5.2): a line that starts with "." in the body must become "..", or the mail server reads it as the end-of-DATA marker.
function dotStuff(text) { return String(text).replace(/\r\n/g, '\n').split('\n').map(l => (l.startsWith('.') ? '.' + l : l)).join('\r\n'); }

function buildMessage({ from, to, subject, text }) {
  const id = crypto.randomBytes(12).toString('hex') + '@audio-mixer';
  const headers = [`From: ${from}`, `To: ${to}`, `Subject: ${String(subject || '').replace(/[\r\n]/g, ' ')}`, `Date: ${new Date().toUTCString()}`, `Message-ID: <${id}>`, 'MIME-Version: 1.0', 'Content-Type: text/plain; charset=utf-8', 'Content-Transfer-Encoding: 8bit'];
  return headers.join('\r\n') + '\r\n\r\n' + dotStuff(text) + '\r\n.';
}

// Sends one plain-text email. Options beyond { to, subject, text } (host, port, secure, user, pass, from) default from the SMTP_* environment
// variables; pass them explicitly (as the tests do) to talk to a fake server instead of reading the real environment.
async function send({ to, subject, text, host, port, secure, user, pass, from, env = process.env, connect = net.connect, connectTLS = tls.connect, timeoutMs = 10000 } = {}) {
  const cfg = { ...fromEnv(env), ...(host !== undefined ? { host } : {}), ...(port !== undefined ? { port } : {}), ...(secure !== undefined ? { secure } : {}), ...(user !== undefined ? { user } : {}), ...(pass !== undefined ? { pass } : {}), ...(from !== undefined ? { from } : {}) };
  if (!cfg.host) return { sent: false, reason: 'not configured' };
  if (!EMAIL_RE.test(String(to || ''))) throw new Error('to is not an e-mail address');
  if (!cfg.from) throw new Error('SMTP_FROM (or SMTP_USER) is not set');
  let sock = await new Promise((resolve, reject) => {
    const s = cfg.secure ? connectTLS({ host: cfg.host, port: cfg.port, servername: cfg.host }) : connect({ host: cfg.host, port: cfg.port });
    s.once('error', reject); s.once(cfg.secure ? 'secureConnect' : 'connect', () => { s.removeListener('error', reject); resolve(s); });
  });
  try {
    await command(sock, null, is2xx, timeoutMs);                                                      // 220 greeting
    let caps = await command(sock, 'EHLO audio-mixer', is2xx, timeoutMs);
    if (!cfg.secure && /STARTTLS/i.test(caps.text)) {
      await command(sock, 'STARTTLS', is2xx, timeoutMs);
      sock = await new Promise((resolve, reject) => { const s = connectTLS({ socket: sock, servername: cfg.host }); s.once('error', reject); s.once('secureConnect', () => resolve(s)); });
      caps = await command(sock, 'EHLO audio-mixer', is2xx, timeoutMs);
    }
    if (cfg.user || cfg.pass) {
      await command(sock, 'AUTH LOGIN', c => c === 334, timeoutMs);
      await command(sock, b64(cfg.user), c => c === 334, timeoutMs);
      await command(sock, b64(cfg.pass), c => c === 235, timeoutMs);
    }
    await command(sock, 'MAIL FROM:<' + cfg.from + '>', is2xx, timeoutMs);
    await command(sock, 'RCPT TO:<' + to + '>', c => is2xx(c) || is3xx(c), timeoutMs);
    await command(sock, 'DATA', c => c === 354, timeoutMs);
    await command(sock, buildMessage({ from: cfg.from, to, subject, text }), is2xx, timeoutMs);
    await command(sock, 'QUIT', () => true, timeoutMs).catch(() => { /* some servers close right after 250 to QUIT */ });
    return { sent: true };
  } finally { try { sock.end(); } catch (_) { /* already gone */ } }
}

module.exports = { send, configured, fromEnv, dotStuff, buildMessage, EMAIL_RE };
