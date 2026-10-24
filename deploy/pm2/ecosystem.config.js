'use strict';
// PM2 (Node.js process manager) config for the Audio Mixer server:
//   npm i -g pm2
//   pm2 start deploy/pm2/ecosystem.config.js        (or: node scripts/service.js pm2 start)
//   pm2 save && pm2 startup                          (start again after a reboot; on Windows use pm2-windows-startup or the NSSM service instead)
// One instance, never a cluster: the audio device and the ASIO driver serve one client. Restarts on a crash with a back-off, and when the memory grows past 400 MB.
const path = require('node:path');
const root = path.resolve(__dirname, '..', '..');
module.exports = {
  apps: [{
    name: 'audio-mixer',
    script: path.join(root, 'bridge', 'server.js'),
    cwd: root,
    instances: 1,
    exec_mode: 'fork',
    autorestart: true,
    exp_backoff_restart_delay: 200,
    max_restarts: 50,
    min_uptime: '10s',
    max_memory_restart: '400M',
    kill_timeout: 5000,
    windowsHide: true,
    time: true,
    env: { NODE_ENV: 'production', BRIDGE_PORT: process.env.BRIDGE_PORT || '8765' },
  }],
};
