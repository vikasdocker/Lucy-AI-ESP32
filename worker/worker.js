// Lucy Cloudflare Worker: zero-configuration browser and ESP32-C3 cloud brain.
// Secrets (never source): OPENAI_API_KEY, LUCY_DEVICE_TOKEN.
// Deployed worker: sparkling-fog-fdc8.vikasshu7.workers.dev
const ALLOWED_ORIGINS = new Set(["http://192.168.4.1", "http://edsoft.local"]);
const DEFAULT_MODEL = "gpt-4o-mini";
const PROMPT = "You are Lucy, a friendly, accurate virtual cat companion on a tiny display. Respond to the user's question in one to three short sentences, using plain ASCII text without Markdown and under 40 words. When uncertain, say so. Prefer romanized Marathi or Hindi if asked, because the display has limited glyphs. Do not pretend to know current events without a source.";
const JSON_HEADERS = {"Content-Type": "application/json; charset=utf-8", "Cache-Control":"no-store", "X-Content-Type-Options":"nosniff"};
const json = (value, status=200, extra={})=>Response.json(value,{status,headers:{...JSON_HEADERS,...extra}});
function validMessages(v) {
  if(!Array.isArray(v)||v.length<1||v.length>16) return null;
  const out=[]; let total=0;
  for(const m of v) {
    if(!m||!["system","user","assistant"].includes(m.role)||typeof m.content!=="string"||!m.content.trim()||m.content.length>2000)return null;
    total+=m.content.length; if(total>12000)return null;
    // Ignore untrusted system instructions supplied by clients.
    if(m.role!=="system")out.push({role:m.role,content:m.content});
  }
  return out.length&&out[out.length-1].role==="user"?out:null;
}
async function equalSecret(a,b) {
  if(!a||!b) return false;
  const enc=new TextEncoder();
  const [x,y]=await Promise.all([crypto.subtle.digest("SHA-256",enc.encode(a)),crypto.subtle.digest("SHA-256",enc.encode(b))]);
  let diff=0;const xa=new Uint8Array(x),ya=new Uint8Array(y);
  for(let i=0;i<xa.length;i++)diff|=xa[i]^ya[i];
  return diff===0;
}
async function generate(messages,env) {
  if(!env.OPENAI_API_KEY)return {status:503,error:"OpenAI secret is not configured"};
  const model=env.OPENAI_MODEL||DEFAULT_MODEL;
  const ctl=new AbortController();const timer=setTimeout(()=>ctl.abort(),22000);
  try{
    const upstream=await fetch("https://api.openai.com/v1/chat/completions",{
      method:"POST",signal:ctl.signal,headers:{"Authorization":"Bearer "+env.OPENAI_API_KEY,"Content-Type":"application/json"},
      body:JSON.stringify({model,messages:[{role:"system",content:PROMPT},...messages],max_tokens:120,temperature:0.5})
    });
    if(!upstream.ok){
      let code="";try{const e=await upstream.json();code=e?.error?.code||"";}catch{}
      console.log("OpenAI response status",upstream.status,"code",String(code).slice(0,60));
      if(upstream.status===429 && ["insufficient_quota","credit_balance_exhausted"].includes(code))
        return {status:502,error:"OpenAI credits exhausted; add API credits"};
      if(upstream.status===429)return {status:429,error:"OpenAI rate limit"};
      if(upstream.status===401||upstream.status===403)return {status:502,error:"OpenAI secret or project permissions invalid"};
      if(upstream.status===404)return {status:502,error:"OpenAI model not found"};
      return {status:502,error:"OpenAI provider error ("+upstream.status+")"};
    }
    const data=await upstream.json();
    const reply=data?.choices?.[0]?.message?.content;
    if(typeof reply!=="string"||!reply.trim())return {status:502,error:"Empty model response"};
    return {status:200,reply:reply.trim().slice(0,700),model};
  }catch(e){return {status:504,error:e?.name==="AbortError"?"OpenAI timeout":"OpenAI network error"};}
  finally{clearTimeout(timer);}
}
export default {
  async fetch(request,env){
    const url=new URL(request.url),route=url.pathname,origin=request.headers.get("Origin")||"";
    const browser=route==="/";
    const cors=ALLOWED_ORIGINS.has(origin)?{
      "Access-Control-Allow-Origin":origin,
      "Access-Control-Allow-Methods":"POST,OPTIONS",
      "Access-Control-Allow-Headers":"Content-Type,x-lucy-token",
      "Vary":"Origin"
    }:{};
    if(browser && !ALLOWED_ORIGINS.has(origin))return json({error:"Forbidden origin"},403);
    if(request.method==="OPTIONS"){
      if(!browser)return json({error:"Not found"},404);
      return new Response(null,{status:204,headers:cors});
    }
    const device=route==="/api/chat"||route==="/api/tags";
    if(!browser&&!device)return json({error:"Not found"},404);
    if(device || (browser && env.ALLOW_LEGACY_BROWSER !== "true")) {
      if(!env.LUCY_DEVICE_TOKEN||env.LUCY_DEVICE_TOKEN.length<24)return json({error:"Device authentication is not configured"},503);
      if(!await equalSecret(request.headers.get("x-lucy-token"),env.LUCY_DEVICE_TOKEN))return json({error:"Unauthorized"},401);
    }
    if(route==="/api/tags"){
      if(request.method!=="GET")return json({error:"GET required"},405);
      const model=env.OPENAI_MODEL||DEFAULT_MODEL;
      return json({models:[{name:model,model}]});
    }
    if(request.method!=="POST")return json({error:"POST required"},405,cors);
    if(request.headers.get("content-type")?.toLowerCase().split(";")[0].trim()!=="application/json")return json({error:"JSON required"},415,cors);
    if(Number(request.headers.get("content-length")||0)>16000)return json({error:"Request too large"},413,cors);
    // Per-identity, per-location limiter. Not a hard spending cap.
    if(!env.LUCY_RATE_LIMITER)return json({error:"Rate limiting not configured"},503,cors);
    const identity=device?"device":(request.headers.get("CF-Connecting-IP")||"unknown");
    let allowed;
    try { allowed=await env.LUCY_RATE_LIMITER.limit({key:(device?"device:":"browser:")+identity}); }
    catch { return json({error:"Rate limiting unavailable"},503,cors); }
    if(!allowed.success)return json({error:"Too many requests"},429,cors);
    let parsed;
    try {
      const reader=request.body?.getReader();
      if(!reader) return json({error:"Invalid JSON"},400,cors);
      const chunks=[]; let size=0;
      while(true) { const {done,value}=await reader.read(); if(done)break;
        size+=value.byteLength;
        if(size>16000){await reader.cancel();return json({error:"Request too large"},413,cors);}
        chunks.push(value);
      }
      const bytes=new Uint8Array(size);let offset=0;
      for(const chunk of chunks){bytes.set(chunk,offset);offset+=chunk.length;}
      parsed=JSON.parse(new TextDecoder().decode(bytes));
    }catch{return json({error:"Invalid JSON"},400,cors);}
    if(parsed?.stream===true)return json({error:"Streaming is not supported; use stream:false"},400,cors);
    const messages=validMessages(parsed?.messages);
    if(!messages)return json({error:"Invalid messages"},400,cors);
    const outcome=await generate(messages,env);
    if(outcome.status!==200)return json({error:outcome.error},outcome.status,cors);
    if(browser)return json({reply:outcome.reply},200,cors);
    return json({
      model:outcome.model,created_at:new Date().toISOString(),
      message:{role:"assistant",content:outcome.reply},done:true,
      done_reason:"stop"
    });
  }
};
