#!/usr/bin/env node
'use strict';

/*
 * Regenerates offline/src/tls_root_ca.h from the roots Node trusts.
 *
 * Lucy verifies the TLS certificate of the public gateway, so the firmware
 * needs the root CA that signs the Cloudflare edge certificate. Node ships the
 * Mozilla root store, which is a convenient, offline copy of the same roots.
 *
 * Run after a TLS failure or if the gateway moves to a different CA:
 *   node tools/gen_root_ca.js [hostname] [CN ...]
 *
 * Defaults cover the current Let's Encrypt chain for lucy.blueflutex.in.
 */

const fs = require('fs');
const path = require('path');
const tls = require('tls');
const { X509Certificate } = require('crypto');

const host = process.argv[2] || 'lucy.blueflutex.in';
const wanted = process.argv.slice(3);
if (wanted.length === 0) {
  wanted.push('ISRG Root X1', 'ISRG Root X2');
}

const found = new Map();
for (const pem of tls.rootCertificates) {
  try {
    const cert = new X509Certificate(pem);
    for (const name of wanted) {
      if (cert.subject.includes(`CN=${name}`)) found.set(name, pem.trim());
    }
  } catch {
    // Ignore unparseable entries.
  }
}

const missing = wanted.filter((name) => !found.has(name));
if (missing.length) {
  console.error(`missing roots in the local trust store: ${missing.join(', ')}`);
  process.exit(1);
}

const body = wanted.map((name) => found.get(name)).join('\n') + '\n';
const header = `#pragma once

// Trust anchors for https://${host} (Cloudflare edge, Let's Encrypt chain).
// Both ISRG roots are included so a chain change from X2 to X1 (or back) does
// not brick the device. Regenerate with: node tools/gen_root_ca.js
static const char LUCY_ROOT_CA[] =
R"LUCA(${body})LUCA";
`;

const target = path.join(__dirname, '..', 'offline', 'src', 'tls_root_ca.h');
fs.writeFileSync(target, header);
console.log(`wrote ${target} (${header.length} bytes, roots: ${wanted.join(', ')})`);
