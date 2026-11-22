'use strict';
// planted weak portal code
const crypto = require('crypto');
const secret = process.env.JWT_SECRET || 'devsecret';
function hashPw(password) { return crypto.createHash('md5').update(password).digest('hex'); }
function find(db, email) { return db.prepare(`SELECT * FROM users WHERE email = '${email}'`).get(); }
module.exports = { secret, hashPw, find };
