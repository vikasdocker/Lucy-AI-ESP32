#!/usr/bin/env node
'use strict';

/*
 * Generates gateway/config.json with a strong random token.
 *
 *   node gateway/setup.js            create config.json if missing
 *   node gateway/setup.js --rotate   replace the token (re-provision Lucy after)
 *
 * The token is never printed. It is only written to config.json.
 */

const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

const CONFIG_PATH = path.join(__dirname, 'config.json');
const EXAMPLE_PATH = path.join(__dirname, 'config.example.json');
const rotate = process.argv.includes('--rotate');

let config = {};
if (fs.existsSync(CONFIG_PATH) && !rotate) {
  config = JSON.parse(fs.readFileSync(CONFIG_PATH, 'utf8'));
  console.log('config.json already exists; nothing changed (use --rotate to replace the token)');
  process.exit(0);
}

if (fs.existsSync(EXAMPLE_PATH)) {
  config = JSON.parse(fs.readFileSync(EXAMPLE_PATH, 'utf8'));
}

config.token = crypto.randomBytes(24).toString('hex');

// Keep any hand-edited values from an existing config when rotating.
if (rotate && fs.existsSync(CONFIG_PATH)) {
  const previous = JSON.parse(fs.readFileSync(CONFIG_PATH, 'utf8'));
  config = { ...previous, token: config.token };
}

fs.writeFileSync(CONFIG_PATH, JSON.stringify(config, null, 2) + '\n', { mode: 0o600 });
console.log(`wrote ${CONFIG_PATH}`);
console.log(`token: hidden (${config.token.length} chars)`);
console.log('Next: provision Lucy with tools/provision_lucy_ai.py, then start gateway + tunnel.');
