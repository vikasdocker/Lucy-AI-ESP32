#!/usr/bin/env node
'use strict';

/*
 * Lucy model gateway — a tiny, authenticated proxy in front of Ollama.
 *
 * Why this exists: Ollama has no authentication. Exposing port 11434 to the
 * internet would let anyone use (or abuse) the GPU. This process is the only
 * thing the public tunnel can reach. It requires a token, rate limits per IP,
 * clamps request size, and only forwards to two Ollama endpoints.
 *
 * It deliberately speaks Ollama's API shape (/api/chat, /api/tags), so Lucy's
 * firmware talks to it exactly like a LAN Ollama server — just over HTTPS.
 *
 *   POST /api/chat   {model?, messages:[...], options?}  -> Ollama chat response
 *   POST /chat       {messages:[...]}                    -> {reply, model} (curl-friendly)
 *   GET  /api/tags                                       -> {models:[{name}]}
 *   GET  /health                                         -> {ok, service, uptime, model}
 *
 * Config: ./config.json (see config.example.json) or env overrides.
 * Secrets are never logged.
 */

const http = require('http');
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

const CONFIG_PATH = path.join(__dirname, 'config.json');

function loadConfig() {
  let file = {};
  try {
    file = JSON.parse(fs.readFileSync(CONFIG_PATH, 'utf8'));
  } catch (error) {
    if (error.code !== 'ENOENT') {
      console.error(`[config] cannot read ${CONFIG_PATH}: ${error.message}`);
      process.exit(1);
    }
  }

  const config = {
    port: Number(process.env.LUCY_GATEWAY_PORT || file.port || 8788),
    bind: process.env.LUCY_GATEWAY_BIND || file.bind || '127.0.0.1',
    token: process.env.LUCY_GATEWAY_TOKEN || file.token || '',
    ollamaUrl: (process.env.OLLAMA_URL || file.ollamaUrl || 'http://127.0.0.1:11434').replace(/\/+$/, ''),
    model: process.env.LUCY_MODEL || file.model || 'qwen2.5:7b',
    timeoutMs: Number(file.timeoutMs || 90000),
    rateLimit: {
      windowMs: Number((file.rateLimit && file.rateLimit.windowMs) || 60000),
      max: Number((file.rateLimit && file.rateLimit.max) || 60),
    },
    maxConcurrent: Number(file.maxConcurrent || 2),
    maxBodyBytes: Number(file.maxBodyBytes || 65536),
    maxMessages: Number(file.maxMessages || 20),
    maxCharsPerMessage: Number(file.maxCharsPerMessage || 4000),
  };

  if (!config.token || config.token.length < 24 || /^CHANGE_ME/i.test(config.token)) {
    console.error('[config] refusing to start: set a real token (>=24 chars) in config.json');
    process.exit(1);
  }
  return config;
}

const config = loadConfig();
const startedAt = Date.now();
let inFlight = 0;

// ---------------------------------------------------------------------------
// Rate limiting (per client IP, fixed window)
// ---------------------------------------------------------------------------
const buckets = new Map();

function allow(ip) {
  const now = Date.now();
  const bucket = buckets.get(ip);
  if (!bucket || now > bucket.reset) {
    buckets.set(ip, { count: 1, reset: now + config.rateLimit.windowMs });
    return true;
  }
  if (bucket.count >= config.rateLimit.max) return false;
  bucket.count += 1;
  return true;
}

setInterval(() => {
  const now = Date.now();
  for (const [ip, bucket] of buckets) {
    if (now > bucket.reset) buckets.delete(ip);
  }
}, 60000).unref();

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
function log(message) {
  console.log(`${new Date().toISOString()} ${message}`);
}

function clientIp(req) {
  const forwarded = req.headers['cf-connecting-ip'] || req.headers['x-forwarded-for'];
  if (typeof forwarded === 'string' && forwarded.length) {
    return forwarded.split(',')[0].trim();
  }
  return (req.socket.remoteAddress || 'unknown').replace(/^::ffff:/, '');
}

function sendJson(res, status, payload) {
  const body = JSON.stringify(payload);
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store',
    'Content-Length': Buffer.byteLength(body),
  });
  res.end(body);
}

function tokenMatches(given) {
  if (typeof given !== 'string' || given.length === 0) return false;
  const a = Buffer.from(given);
  const b = Buffer.from(config.token);
  return a.length === b.length && crypto.timingSafeEqual(a, b);
}

function authorized(req) {
  const header = req.headers['x-lucy-token'] || req.headers['x-bridge-token'];
  if (tokenMatches(header)) return true;
  const auth = req.headers['authorization'];
  if (typeof auth === 'string' && auth.startsWith('Bearer ')) {
    return tokenMatches(auth.slice(7).trim());
  }
  return false;
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let size = 0;
    const chunks = [];
    req.on('data', (chunk) => {
      size += chunk.length;
      if (size > config.maxBodyBytes) {
        reject(Object.assign(new Error('Request body too large'), { statusCode: 413 }));
        req.destroy();
        return;
      }
      chunks.push(chunk);
    });
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
    req.on('error', reject);
  });
}

// Keeps the public surface predictable: only these roles, bounded length.
function sanitizeMessages(raw) {
  if (!Array.isArray(raw) || raw.length === 0) {
    throw Object.assign(new Error('messages must be a non-empty array'), { statusCode: 400 });
  }
  if (raw.length > config.maxMessages) {
    throw Object.assign(new Error(`too many messages (max ${config.maxMessages})`), { statusCode: 400 });
  }
  return raw.map((message) => {
    if (!message || typeof message.content !== 'string') {
      throw Object.assign(new Error('each message needs string content'), { statusCode: 400 });
    }
    const role = ['system', 'user', 'assistant'].includes(message.role) ? message.role : 'user';
    return { role, content: message.content.slice(0, config.maxCharsPerMessage) };
  });
}

function sanitizeOptions(raw) {
  const options = {};
  if (raw && typeof raw === 'object') {
    if (Number.isFinite(raw.temperature)) {
      options.temperature = Math.min(Math.max(Number(raw.temperature), 0), 1.2);
    }
    if (Number.isFinite(raw.num_predict)) {
      options.num_predict = Math.min(Math.max(Math.trunc(Number(raw.num_predict)), 1), 200);
    }
    if (Number.isFinite(raw.top_p)) {
      options.top_p = Math.min(Math.max(Number(raw.top_p), 0), 1);
    }
  }
  return options;
}

// ---------------------------------------------------------------------------
// Ollama call
// ---------------------------------------------------------------------------
async function chatWithOllama(messages, options) {
  const body = JSON.stringify({
    model: config.model, // client cannot choose the model
    messages,
    stream: false, // streaming is never proxied
    keep_alive: '10m',
    options,
  });

  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), config.timeoutMs);
  try {
    const response = await fetch(`${config.ollamaUrl}/api/chat`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body,
      signal: controller.signal,
    });
    if (!response.ok) {
      const text = await response.text().catch(() => '');
      throw Object.assign(new Error(`ollama ${response.status}: ${text.slice(0, 200)}`), {
        statusCode: 502,
      });
    }
    const data = await response.json();
    const content = data && data.message && data.message.content;
    if (typeof content !== 'string' || !content.trim()) {
      throw Object.assign(new Error('empty model reply'), { statusCode: 502 });
    }
    return {
      model: config.model,
      created_at: data.created_at,
      message: { role: 'assistant', content: content.trim() },
      done: true,
      done_reason: data.done_reason,
      eval_count: data.eval_count,
    };
  } catch (error) {
    if (error.name === 'AbortError') {
      throw Object.assign(new Error('model timed out'), { statusCode: 504 });
    }
    throw error;
  } finally {
    clearTimeout(timer);
  }
}

// ---------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------
const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
  const ip = clientIp(req);

  if (req.method === 'OPTIONS') {
    res.writeHead(204, {
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
      'Access-Control-Allow-Headers': 'Content-Type, X-Lucy-Token, Authorization',
    });
    res.end();
    return;
  }

  // Public liveness probe. Deliberately reveals nothing sensitive.
  if (req.method === 'GET' && (url.pathname === '/health' || url.pathname === '/')) {
    sendJson(res, 200, {
      ok: true,
      service: 'lucy-model-gateway',
      uptime: Math.floor((Date.now() - startedAt) / 1000),
      model: config.model,
      busy: inFlight,
    });
    return;
  }

  if (!authorized(req)) {
    log(`[auth] rejected ${ip} ${req.method} ${url.pathname}`);
    sendJson(res, 401, { error: 'Invalid token' });
    return;
  }

  if (req.method === 'GET' && url.pathname === '/api/tags') {
    // Shape Lucy's ping expects; only the served model is listed.
    sendJson(res, 200, { models: [{ name: config.model, model: config.model }] });
    return;
  }

  const isChat = req.method === 'POST' && (url.pathname === '/api/chat' || url.pathname === '/chat');
  if (!isChat) {
    sendJson(res, 404, { error: 'Not found' });
    return;
  }

  if (!allow(ip)) {
    log(`[rate] limited ${ip}`);
    sendJson(res, 429, { error: 'Too many messages. Wait a moment and try again.' });
    return;
  }

  if (inFlight >= config.maxConcurrent) {
    sendJson(res, 429, { error: 'Model busy. Try again in a moment.' });
    return;
  }

  let payload;
  try {
    payload = JSON.parse(await readBody(req));
  } catch (error) {
    sendJson(res, error.statusCode || 400, { error: error.message });
    return;
  }

  let messages;
  try {
    const source = Array.isArray(payload) ? payload : payload.messages;
    messages = sanitizeMessages(source);
  } catch (error) {
    sendJson(res, error.statusCode || 400, { error: error.message });
    return;
  }

  const options = sanitizeOptions(payload && payload.options);
  const started = Date.now();
  inFlight += 1;
  try {
    const result = await chatWithOllama(messages, options);
    const ms = Date.now() - started;
    log(
      `[chat] ${ip} ${messages.length} msg, reply ${result.message.content.length} chars in ${ms} ms`
    );
    if (url.pathname === '/chat') {
      sendJson(res, 200, { reply: result.message.content, model: config.model });
    } else {
      sendJson(res, 200, result);
    }
  } catch (error) {
    log(`[chat] error ${error.statusCode || 500}: ${error.message}`);
    sendJson(res, error.statusCode || 500, { error: error.message });
  } finally {
    inFlight -= 1;
  }
});

server.requestTimeout = config.timeoutMs + 15000;
server.headersTimeout = 20000;
server.listen(config.port, config.bind, () => {
  log(`lucy-model-gateway listening on http://${config.bind}:${config.port}`);
  log(`model: ${config.model}  ->  ${config.ollamaUrl}`);
  log(`auth: token required (${config.token.length} chars), rate ${config.rateLimit.max}/${config.rateLimit.windowMs} ms`);
});

process.on('SIGINT', () => {
  log('shutting down');
  server.close(() => process.exit(0));
});
