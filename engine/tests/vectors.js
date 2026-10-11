'use strict';
// Reads vectors.txt into cases: { kind, id, head: [fields], expect: [numbers], strips: [...] (mixer) }
const fs = require('node:fs');
const path = require('node:path');
const num = w => (w === '-inf' ? -Infinity : w === 'inf' ? Infinity : Number(w));
function parse(file = path.join(__dirname, 'vectors.txt')) {
  const lines = fs.readFileSync(file, 'utf8').split('\n'), out = [];
  for (let i = 0; i < lines.length; i++) {
    const l = lines[i]; if (!l || l[0] === '#') continue;
    const bar = l.indexOf('|'), head = l.slice(0, bar < 0 ? undefined : bar).trim().split(/\s+/), expect = bar < 0 ? [] : l.slice(bar + 1).trim().split(/\s+/).map(num);
    const c = { kind: head[0], id: head[1], head, expect };
    if (c.kind === 'mixer') { c.strips = []; for (let s = 0; s < Number(head[8]); s++) c.strips.push(lines[++i].trim().split(/\s+/).slice(1)); c.expect = lines[++i].slice(lines[i].indexOf('|') + 1).trim().split(/\s+/).map(num); }
    if (c.kind === 'strip' || c.kind === 'expect') continue;
    out.push(c);
  }
  return out;
}
module.exports = { parse };
