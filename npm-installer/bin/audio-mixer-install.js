#!/usr/bin/env node
'use strict';
const { main } = require('../lib/cli');
main(process.argv.slice(2)).then(code => process.exit(code), e => { console.error('Error: ' + e.message); process.exit(1); });
