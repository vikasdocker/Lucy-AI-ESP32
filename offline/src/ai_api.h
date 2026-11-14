#pragma once

#include <stdint.h>
#include <stddef.h>

// Client for a model server: either Ollama on the local PC
// (http://<pc>:11434) or the token-protected public gateway
// (https://lucy.blueflutex.in).
//
// Both speak the same API (/api/chat, /api/tags), so only the base URL,
// the token and the transport change. Lucy stays offline-first: when the
// server is unreachable, aiReply() returns nullptr and the caller falls back
// to the built-in keyword/intent/Markov brain.

// Base URL without a trailing slash. Accepts http:// and https://.
void aiSetUrl(const char *url);
// Convenience for LAN Ollama: builds http://host:port.
void aiSetEndpoint(const char *host, uint16_t port);
void aiSetModel(const char *model);
// Sent as x-lucy-token. Empty = no header (LAN Ollama ignores it anyway).
void aiSetToken(const char *token);
// Escape hatch: skip certificate verification if the CA ever changes.
void aiSetInsecureTls(bool insecure);

const char *aiUrl();
const char *aiModel();
// For NVS persistence only; never print this.
const char *aiToken();
bool aiInsecure();
// Length only, so status output never prints the secret.
size_t aiTokenLength();
bool aiUsesTls();

// Sends one chat turn (with a short rolling history).
// Returns a static reply buffer, or nullptr on any failure.
// Failures trigger a 30 s circuit breaker so chat stays snappy when the
// server is down.
const char *aiReply(const char *text);

// GET /api/tags with the token. true = the server answered 200.
bool aiPing(unsigned long timeoutMs = 8000);

// Last HTTP status seen by a request (0 = no response / transport error).
// Lets `AI` distinguish "unreachable" from "wrong token" (401).
int aiLastStatus();

// Forget the circuit breaker (e.g. after the user fixes the network).
void aiClearRetry();
