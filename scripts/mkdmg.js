'use strict';
// Writes an Apple disk image (.dmg, UDIF, zlib-compressed "UDZO") from a plain disk image file (here: an ISO 9660 + Rock Ridge volume made by genisoimage),
// without hdiutil, so a macOS .dmg can be built on Linux and Windows. macOS mounts it like any other .dmg (same as `hdiutil convert x.iso -format UDZO`).
// Layout: [compressed 1 MiB chunks][XML property list with one 'mish' block table][512-byte 'koly' trailer], all numbers big endian.
// The checksum fields are written as "none" (type 0): they are optional in UDIF, and a wrong checksum would make macOS refuse the image.
const fs = require('node:fs');
const zlib = require('node:zlib');
const crypto = require('node:crypto');

const SECTOR = 512, CHUNK_SECTORS = 2048;          // 1 MiB per run
const T_ZLIB = 0x80000005, T_RAW = 0x00000001, T_ZERO = 0x00000002, T_END = 0xffffffff;

function mish(sectors, runs, endOffset) {
  const b = Buffer.alloc(204 + 40 * (runs.length + 1));
  b.write('mish', 0, 'latin1'); b.writeUInt32BE(1, 4);
  b.writeBigUInt64BE(0n, 8); b.writeBigUInt64BE(BigInt(sectors), 16); b.writeBigUInt64BE(0n, 24);
  b.writeUInt32BE(CHUNK_SECTORS, 32); b.writeUInt32BE(0xffffffff, 36);                     // buffers needed, descriptor: whole disk
  // 40..63 reserved, 64: checksum type 0 (none), size 0, 128 bytes of data -> offset 200 is the run count
  b.writeUInt32BE(runs.length + 1, 200);
  let o = 204;
  for (const r of runs.concat([{ type: T_END, start: sectors, count: 0, offset: endOffset, length: 0 }])) {
    b.writeUInt32BE(r.type, o); b.writeUInt32BE(0, o + 4);
    b.writeBigUInt64BE(BigInt(r.start), o + 8); b.writeBigUInt64BE(BigInt(r.count), o + 16);
    b.writeBigUInt64BE(BigInt(r.offset), o + 24); b.writeBigUInt64BE(BigInt(r.length), o + 32);
    o += 40;
  }
  return b;
}

function koly({ dataLength, xmlOffset, xmlLength, sectors, id }) {
  const k = Buffer.alloc(512);
  k.write('koly', 0, 'latin1'); k.writeUInt32BE(4, 4); k.writeUInt32BE(512, 8); k.writeUInt32BE(1, 12);
  k.writeBigUInt64BE(0n, 16); k.writeBigUInt64BE(0n, 24); k.writeBigUInt64BE(BigInt(dataLength), 32);       // data fork: offset 0, length
  k.writeBigUInt64BE(0n, 40); k.writeBigUInt64BE(0n, 48);                                                  // no resource fork
  k.writeUInt32BE(1, 56); k.writeUInt32BE(1, 60); id.copy(k, 64);                                           // segment 1 of 1, segment id
  k.writeUInt32BE(0, 80); k.writeUInt32BE(0, 84);                                                          // data checksum: none
  k.writeBigUInt64BE(BigInt(xmlOffset), 216); k.writeBigUInt64BE(BigInt(xmlLength), 224);
  k.writeUInt32BE(0, 352); k.writeUInt32BE(0, 356);                                                        // master checksum: none
  k.writeUInt32BE(1, 488); k.writeBigUInt64BE(BigInt(sectors), 492);                                       // image variant 1, sector count
  return k;
}

function plist(table, name) {
  const b64 = table.toString('base64').replace(/(.{76})/g, '$1\n\t\t\t');
  return `<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n<plist version="1.0">\n<dict>\n\t<key>resource-fork</key>\n\t<dict>\n\t\t<key>blkx</key>\n\t\t<array>\n\t\t\t<dict>\n` +
    `\t\t\t\t<key>Attributes</key>\n\t\t\t\t<string>0x0050</string>\n\t\t\t\t<key>CFName</key>\n\t\t\t\t<string>${name}</string>\n\t\t\t\t<key>Data</key>\n\t\t\t\t<data>\n\t\t\t${b64}\n\t\t\t\t</data>\n\t\t\t\t<key>ID</key>\n\t\t\t\t<string>0</string>\n\t\t\t\t<key>Name</key>\n\t\t\t\t<string>${name}</string>\n\t\t\t</dict>\n\t\t</array>\n\t</dict>\n</dict>\n</plist>\n`;
}

// source: Buffer or file path of the raw image; returns { dmg, sectors, compressed }
function makeDmg(source, dest, { name = 'disk image (Apple_HFS : 0)' } = {}) {
  let raw = Buffer.isBuffer(source) ? source : fs.readFileSync(source);
  if (raw.length % SECTOR) raw = Buffer.concat([raw, Buffer.alloc(SECTOR - (raw.length % SECTOR))]);
  const sectors = raw.length / SECTOR, parts = [], runs = [];
  let offset = 0;
  for (let s = 0; s < sectors; s += CHUNK_SECTORS) {
    const count = Math.min(CHUNK_SECTORS, sectors - s), chunk = raw.subarray(s * SECTOR, (s + count) * SECTOR);
    if (!chunk.some(x => x !== 0)) { runs.push({ type: T_ZERO, start: s, count, offset, length: 0 }); continue; }   // empty space costs nothing
    const z = zlib.deflateSync(chunk, { level: 9 });
    if (z.length < chunk.length) { parts.push(z); runs.push({ type: T_ZLIB, start: s, count, offset, length: z.length }); offset += z.length; }
    else { parts.push(Buffer.from(chunk)); runs.push({ type: T_RAW, start: s, count, offset, length: chunk.length }); offset += chunk.length; }
  }
  const xml = Buffer.from(plist(mish(sectors, runs, offset), name), 'utf8');
  const trailer = koly({ dataLength: offset, xmlOffset: offset, xmlLength: xml.length, sectors, id: crypto.randomBytes(16) });
  fs.writeFileSync(dest, Buffer.concat([...parts, xml, trailer]));
  return { dmg: dest, sectors, compressed: offset + xml.length + 512 };
}

module.exports = { makeDmg, mish, koly, plist, SECTOR, CHUNK_SECTORS };
