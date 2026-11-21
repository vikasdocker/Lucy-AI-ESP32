#!/usr/bin/env node
'use strict';
// Generate a private, git-ignored C3 firmware credential header.
// Use only in a trusted local build environment. Never print the token.
const fs=require('node:fs');
const path=require('node:path');
const secret=process.env.LUCY_DEVICE_TOKEN||'';
if(secret.length<24||secret.length>95||/[^!-~]/.test(secret)) {
 console.error('LUCY_DEVICE_TOKEN must be 24-95 visible non-space ASCII characters.');
 process.exit(1);
}
const target=path.join(__dirname,'..','offline','src','lucy_device_config.h');
fs.writeFileSync(target,'#pragma once\n#define LUCY_DEVICE_TOKEN '+JSON.stringify(secret)+'\n',{mode:0o600});
console.log('Prepared private firmware device header (value not printed).');
