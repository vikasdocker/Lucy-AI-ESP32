import assert from 'node:assert/strict';
import {mkdtempSync,mkdirSync,copyFileSync,readFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
const dir=mkdtempSync(path.join(tmpdir(),'lucy-token-'));
mkdirSync(path.join(dir,'tools'));
mkdirSync(path.join(dir,'offline','src'),{recursive:true});
const script=path.join(dir,'tools','write_device_token.js');
copyFileSync('tools/write_device_token.js',script);
for(const [token,valid] of [['short',false],['x'.repeat(24),true],['x'.repeat(24)+' ',false],['x'.repeat(24)+'é',false],['x'.repeat(96),false],['x'.repeat(24)+'"\\',true]]) {
 const run=spawnSync(process.execPath,[script],{env:{...process.env,LUCY_DEVICE_TOKEN:token},encoding:'utf8'});
 assert.equal(run.status===0,valid);
 assert.ok(!run.stdout.includes(token)&&!run.stderr.includes(token));
 if(valid)assert.ok(readFileSync(path.join(dir,'offline','src','lucy_device_config.h'),'utf8').includes(JSON.stringify(token)));
}
console.log('Private header validation, escaping and non-disclosure checks passed.');
