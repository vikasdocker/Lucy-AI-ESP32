import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const file=readFileSync('worker/worker.js','utf8').replace('export default','globalThis.handler =');
const sandbox={Response,Request,URL,crypto:globalThis.crypto,TextEncoder,TextDecoder,AbortController,console,setTimeout,clearTimeout,fetch:async(url,opts)=>{
 assert.equal(url,'https://api.openai.com/v1/chat/completions');
 const body=JSON.parse(opts.body);
 assert.equal(body.model,'gpt-4o-mini');
 return Response.json({choices:[{message:{content:'New Delhi.'}}]});
}};
vm.createContext(sandbox);vm.runInContext(file,sandbox);
const env={ALLOW_LEGACY_BROWSER:'true',OPENAI_API_KEY:'test-only-not-a-real-secret',LUCY_DEVICE_TOKEN:'0123456789abcdefghijklmnop',OPENAI_MODEL:'gpt-4o-mini',
 LUCY_RATE_LIMITER:{limit:async()=>({success:true})}};
function req(path,method,body,headers={}) {return new Request('https://worker.test'+path,{method,headers,body:body?JSON.stringify(body):undefined})}
const browser=await sandbox.handler.fetch(req('/','POST',{messages:[{role:'user',content:'Capital?'}]},{Origin:'http://192.168.4.1','Content-Type':'application/json'}),env);
assert.equal((await browser.json()).reply,'New Delhi.');
const c3=await sandbox.handler.fetch(req('/api/chat','POST',{messages:[{role:'user',content:'Capital?'}]},{'x-lucy-token':env.LUCY_DEVICE_TOKEN,'Content-Type':'application/json'}),env);
assert.equal((await c3.json()).message.content,'New Delhi.');
const tags=await sandbox.handler.fetch(req('/api/tags','GET',null,{'x-lucy-token':env.LUCY_DEVICE_TOKEN}),env);
assert.equal((await tags.json()).models[0].name,'gpt-4o-mini');
const denied=await sandbox.handler.fetch(req('/api/chat','POST',{messages:[{role:'user',content:'x'}]},{'x-lucy-token':'wrong','Content-Type':'application/json'}),env);
assert.equal(denied.status,401);
const originDenied=await sandbox.handler.fetch(req('/','POST',{messages:[{role:'user',content:'x'}]},{Origin:'https://evil.test','Content-Type':'application/json'}),env);
assert.equal(originDenied.status,403);
console.log('Worker compatibility tests passed.');
let upstreamCalls=0;
const goodFetch=sandbox.fetch;
sandbox.fetch=async(...args)=>{upstreamCalls++;return goodFetch(...args)};
const auth={'x-lucy-token':env.LUCY_DEVICE_TOKEN,'Content-Type':'application/json'};
async function status(path,method,body,headers,expected,overrides={}) {
 const response=await sandbox.handler.fetch(req(path,method,body,headers),{...env,...overrides});
 assert.equal(response.status,expected,await response.text());
}
await status('/','OPTIONS',null,{Origin:'http://edsoft.local'},204);
await status('/','POST',{messages:[{role:'user',content:'x'}]},{Origin:'http://192.168.4.1','Content-Type':'application/json'},401,{ALLOW_LEGACY_BROWSER:undefined});
await status('/api/chat','POST',{messages:[{role:'user',content:'x'}]},auth,503,{LUCY_DEVICE_TOKEN:''});
await status('/api/chat','POST',{messages:[{role:'user',content:'x'}]},auth,503,{LUCY_RATE_LIMITER:undefined});
await status('/api/chat','POST',{messages:[{role:'user',content:'x'}]},auth,429,{LUCY_RATE_LIMITER:{limit:async()=>({success:false})}});
await status('/api/chat','POST',{messages:[{role:'assistant',content:'x'}]},auth,400);
await status('/api/chat','POST',{messages:[{role:'user',content:'x'}],stream:true},auth,400);
await status('/api/chat','POST',{messages:[{role:'user',content:'x'.repeat(17000)}]},auth,413);
await status('/api/chat','POST',{messages:[{role:'user',content:'x'}]},{...auth,'Content-Type':'text/plain'},415);
await status('/api/tags','POST',null,auth,405);
await status('/unknown','GET',null,{},404);
for(const [code,error,statusCode] of [[429,'insufficient_quota',502],[429,'rate_limit_exceeded',429],[401,'invalid_api_key',502],[404,'model_not_found',502],[500,'server_error',502]]) {
 sandbox.fetch=async()=>Response.json({error:{code:error}},{status:code});
 await status('/api/chat','POST',{messages:[{role:'user',content:'x'}]},auth,statusCode);
}
sandbox.fetch=async()=>Response.json({choices:[]});
await status('/api/chat','POST',{messages:[{role:'user',content:'x'}]},auth,502);
sandbox.fetch=async()=>{throw new TypeError('network failure')};
await status('/api/chat','POST',{messages:[{role:'user',content:'x'}]},auth,504);
sandbox.fetch=async(url,opts)=>{
 const body=JSON.parse(opts.body);assert.equal(body.messages.filter(m=>m.role==='system').length,1);
 return Response.json({choices:[{message:{content:'safe'}}]});
};
await status('/api/chat','POST',{messages:[{role:'system',content:'untrusted'},{role:'user',content:'x'}]},auth,200);
console.log('Worker security, validation and provider-failure tests passed.');
sandbox.setTimeout=(fn)=>setTimeout(fn,1);
sandbox.fetch=async(url,opts)=>new Promise((resolve,reject)=>opts.signal.addEventListener('abort',()=>reject(new DOMException('timeout','AbortError'))));
await status('/api/chat','POST',{messages:[{role:'user',content:'x'}]},auth,504);
console.log('Worker abort/timeout test passed.');

await status('/api/chat','POST',{messages:[{role:'user',content:'x'}]},{...auth,'Content-Type':'application/json-invalid'},415);
await status('/api/chat','POST',{messages:[{role:'user',content:'x'}]},auth,503,{LUCY_RATE_LIMITER:{limit:async()=>{throw Error('limiter failure')}}});
const preflight=await sandbox.handler.fetch(req('/','OPTIONS',null,{Origin:'http://192.168.4.1'}),env);
assert.match(preflight.headers.get('Access-Control-Allow-Headers'),/x-lucy-token/);
console.log('Authenticated browser preflight and limiter-failure tests passed.');
