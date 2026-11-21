// AI client for Lucy Offline OS.
//
// Two transports, one API:
//   * LAN:   http://<pc>:11434            (Ollama directly, no auth)
//   * Cloud: https://lucy.blueflutex.in   (token-protected gateway -> Ollama)
//
// Both expose /api/chat and /api/tags, so the request/response code below is
// identical; only the base URL, the token header and TLS differ.
//
// Lucy remains offline-first: every failure returns nullptr quickly so the
// caller falls back to the built-in keyword/intent/Markov brain.

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <string.h>
#include <time.h>
#include <atomic>
#include <ArduinoJson.h>

#include "ai_api.h"
#include "tls_root_ca.h"
#include "lucy_cloud_config.h"

namespace {

constexpr uint16_t DEFAULT_PORT = 11434;
constexpr size_t AI_OUT_MAX = 768;
constexpr size_t AI_USER_MAX = 160;
constexpr size_t AI_BOT_MAX = 512;
constexpr int AI_HISTORY = 3;
constexpr unsigned long RETRY_BACKOFF_MS = 30000;
constexpr unsigned long LAN_TIMEOUT_MS = 15000;
constexpr unsigned long CLOUD_TIMEOUT_MS = 26000;

#if defined(LUCY_CLIPBOT_C3)
char baseUrl[96] = LUCY_WORKER_URL;
#else
char baseUrl[96] = "http://192.168.4.2:11434";
#endif
char modelCfg[40] = "qwen2.5:7b";
std::atomic<uint8_t> replyLanguage{0};
char tokenCfg[96] = "";
QueueHandle_t completed = nullptr;
bool chatBusy = false;
char pendingText[AI_USER_MAX];

unsigned long retryAt = 0;
std::atomic<int> lastStatus{0};
char outBuf[AI_OUT_MAX];

// Rolling history so the model can follow a short conversation.
char histUser[AI_HISTORY][AI_USER_MAX];
char histBot[AI_HISTORY][AI_BOT_MAX];
int histCount = 0;

const char SYSTEM_PROMPT[] =
    "You are Lucy, a warm, playful companion living on a tiny offline ESP32 board. "
    "Reply in 1 to 3 short sentences, under 40 words. "
    "Write in plain Latin characters only (ASCII). If the user writes Hindi or Marathi, "
    "answer in romanized Latin letters. Never output Chinese, Devanagari, or other scripts. "
    "Never claim internet access, browsing, or live data. Be concrete, kind, and a little curious.";

bool usesTls() { return strncmp(baseUrl, "https://", 8) == 0; }

// True when the configured transport can plausibly work right now.
bool networkReady() {
  if (usesTls()) return WiFi.status() == WL_CONNECTED;      // cloud needs STA
  return WiFi.softAPgetStationNum() > 0 || WiFi.status() == WL_CONNECTED;
}

String endpoint(const char *path) {
  if (baseUrl[0] == '\0') return String(path);
  return String(baseUrl) + path;
}

void appendEscaped(String &out, const char *text) {
  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
    switch (*p) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': break;
      case '\t': out += "\\t"; break;
      default:
        if (*p < 0x20) out += ' ';
        else out += (char)*p;
        break;
    }
  }
}

void pushHistory(const char *user, const char *bot) {
  if (histCount == AI_HISTORY) {
    memmove(histUser[0], histUser[1], sizeof(histUser[0]) * (AI_HISTORY - 1));
    memmove(histBot[0], histBot[1], sizeof(histBot[0]) * (AI_HISTORY - 1));
    histCount = AI_HISTORY - 1;
  }
  strncpy(histUser[histCount], user, AI_USER_MAX - 1);
  histUser[histCount][AI_USER_MAX - 1] = '\0';
  strncpy(histBot[histCount], bot, AI_BOT_MAX - 1);
  histBot[histCount][AI_BOT_MAX - 1] = '\0';
  histCount++;
}

// Shared HTTP setup so chat and ping behave the same.
bool beginRequest(HTTPClient &http, WiFiClient &plain, WiFiClientSecure &secure,
                  const String &url) {
  if (usesTls()) {
    secure.setCACert(LUCY_ROOT_CA);
    secure.setHandshakeTimeout(10);
    if (!http.begin(secure, url)) return false;
    http.setTimeout(CLOUD_TIMEOUT_MS);
    http.setConnectTimeout(10000);
  } else {
    if (!http.begin(plain, url)) return false;
    http.setTimeout(LAN_TIMEOUT_MS);
    http.setConnectTimeout(3000);
  }
  http.setReuse(false);
  http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  return true;
}

void addAuth(HTTPClient &http) {
#if defined(LUCY_CLIPBOT_C3)
  if (tokenCfg[0] != '\0') http.addHeader("x-lucy-token", tokenCfg);
#else
  if (tokenCfg[0] != '\0') http.addHeader("x-lucy-token", tokenCfg);
#endif
}

const char *requestChat(const char *text) {
  if (!networkReady() || (usesTls() && time(nullptr) < 1704067200)) { lastStatus = 0; return nullptr; }
  String body;
  body.reserve(1024);
#if defined(LUCY_CLIPBOT_C3)
  // Worker keeps the model fixed and returns an Ollama-compatible reply.
  body += "{\"stream\":false,\"messages\":[";
  for (int i = 0; i < histCount; ++i) {
    if (i) body += ",";
    body += "{\"role\":\"user\",\"content\":\"";
    appendEscaped(body, histUser[i]);
    body += "\"},{\"role\":\"assistant\",\"content\":\"";
    appendEscaped(body, histBot[i]);
    body += "\"}";
  }
  if (histCount) body += ",";
  body += "{\"role\":\"user\",\"content\":\"";
  appendEscaped(body, text);
  body += "\"}]}";
#else
  body += "{\"model\":\"";
  appendEscaped(body, modelCfg);
  body += "\",\"stream\":false,\"keep_alive\":\"10m\",\"messages\":[{\"role\":\"system\",\"content\":\"";
  appendEscaped(body, SYSTEM_PROMPT);
  appendEscaped(body, replyLanguage == 1 ? " Always reply in romanized Hindi." :
      (replyLanguage == 2 ? " Always reply in romanized Marathi." : " Always reply in English."));
  body += "\"}";
  for (int i = 0; i < histCount; i++) {
    body += ",{\"role\":\"user\",\"content\":\"";
    appendEscaped(body, histUser[i]);
    body += "\"},{\"role\":\"assistant\",\"content\":\"";
    appendEscaped(body, histBot[i]);
    body += "\"}";
  }
  body += ",{\"role\":\"user\",\"content\":\"";
  appendEscaped(body, text);
  body += "\"}],\"options\":{\"temperature\":0.7,\"num_predict\":90}}";
#endif

  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure secure;
#if defined(LUCY_CLIPBOT_C3)
  if (!beginRequest(http, plain, secure, endpoint("/api/chat"))) { lastStatus = 0; return nullptr; }
#else
  if (!beginRequest(http, plain, secure, endpoint("/api/chat"))) { lastStatus = 0; return nullptr; }
#endif
  http.addHeader("Content-Type", "application/json");
  addAuth(http);
  lastStatus = http.POST((uint8_t *)body.c_str(), body.length());
  if (lastStatus != 200) { http.end(); return nullptr; }
  StaticJsonDocument<128> filter;
#if defined(LUCY_CLIPBOT_C3)
  filter["message"]["content"] = true;
#else
  filter["message"]["content"] = true;
#endif
  DynamicJsonDocument response(2048);
  const auto err = deserializeJson(response, http.getStream(),
      DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(8));
  http.end();
  if (err) return nullptr;
#if defined(LUCY_CLIPBOT_C3)
  const char *content = response["message"]["content"] | nullptr;
#else
  const char *content = response["message"]["content"] | nullptr;
#endif
  if (!content || !content[0]) return nullptr;
  strncpy(outBuf, content, sizeof(outBuf) - 1);
  outBuf[sizeof(outBuf) - 1] = '\0';
  pushHistory(text, outBuf);
  return outBuf;
}

} // namespace

namespace {
void chatTask(void *) {
  const char *reply = requestChat(pendingText);
  bool ok = reply != nullptr;
  xQueueSend(completed, &ok, portMAX_DELAY);
  vTaskDelete(nullptr);
}
const char *chatWithUi(const char *text) {
  if (chatBusy || !networkReady()) return nullptr;
  if (!completed) completed = xQueueCreate(1, sizeof(bool));
  if (!completed || ESP.getFreeHeap() < 80000) return nullptr;
  strncpy(pendingText, text, sizeof(pendingText)-1);
  pendingText[sizeof(pendingText)-1] = 0;
  chatBusy = true;
  if (xTaskCreate(chatTask, "lucy-chat", 12288, nullptr, 1, nullptr) != pdPASS) chatBusy = false;
  return nullptr;
}
}

void aiSetUrl(const char *url) {
  if (chatBusy) return;
  if (url != nullptr && url[0] != '\0') {
    strncpy(baseUrl, url, sizeof(baseUrl) - 1);
    baseUrl[sizeof(baseUrl) - 1] = '\0';
    // Strip trailing slashes so endpoint() stays simple.
    int len = strlen(baseUrl);
    while (len > 0 && baseUrl[len - 1] == '/') baseUrl[--len] = '\0';
  }
  retryAt = 0;
}

void aiSetEndpoint(const char *host, uint16_t port) {
  if (host == nullptr || host[0] == '\0') return;
  if (strncmp(host, "http://", 7) == 0 || strncmp(host, "https://", 8) == 0) {
    aiSetUrl(host);
    return;
  }
  char url[96];
  snprintf(url, sizeof(url), "http://%s:%u", host, port == 0 ? DEFAULT_PORT : port);
  aiSetUrl(url);
}

void aiSetModel(const char *model) {
  if (chatBusy) return;
  if (model != nullptr && model[0] != '\0') {
    strncpy(modelCfg, model, sizeof(modelCfg) - 1);
    modelCfg[sizeof(modelCfg) - 1] = '\0';
  }
  retryAt = 0;
}
void aiSetLanguage(uint8_t language) { replyLanguage = language <= 2 ? language : 0; }

void aiSetToken(const char *token) {
  if (chatBusy) return;
  if (token == nullptr) return;
  strncpy(tokenCfg, token, sizeof(tokenCfg) - 1);
  tokenCfg[sizeof(tokenCfg) - 1] = '\0';
  retryAt = 0;
}

void aiSetInsecureTls(bool insecure) {
  (void)insecure;
  retryAt = 0;
}

const char *aiUrl() { return baseUrl; }
const char *aiModel() { return modelCfg; }
const char *aiToken() { return tokenCfg; }
bool aiInsecure() { return false; }
size_t aiTokenLength() { return strlen(tokenCfg); }
bool aiUsesTls() { return usesTls(); }
int aiLastStatus() { return lastStatus; }

void aiClearRetry() { retryAt = 0; }
void aiSetIdleHook(void (*hook)()) { (void)hook; }

const char *aiReply(const char *text) {
  if (text == nullptr || text[0] == '\0') return nullptr;
  if (retryAt != 0 && int32_t(millis() - retryAt) < 0) return nullptr;

  const char *reply = chatWithUi(text);
  if (reply == nullptr) {
    // Do not hammer a sleeping server: pause requests for a while.
    retryAt = millis() + RETRY_BACKOFF_MS;
    return nullptr;
  }
  retryAt = 0;
  return reply;
}

bool aiPing(unsigned long timeoutMs) {
  if (chatBusy) return false;
  if (!networkReady()) {
    lastStatus = 0;
    return false;
  }

  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure secure;
#if defined(LUCY_CLIPBOT_C3)
  String url = endpoint("/api/tags");
#else
  String url = endpoint("/api/tags");
#endif
  if (usesTls()) {
    secure.setCACert(LUCY_ROOT_CA);
    secure.setHandshakeTimeout(10);
    if (!http.begin(secure, url)) return false;
  } else {
    if (!http.begin(plain, url)) return false;
  }
  http.setConnectTimeout(timeoutMs);
  http.setTimeout(timeoutMs);
  http.setReuse(false);

  addAuth(http);
  lastStatus = http.GET();
  http.end();
  return lastStatus == 200;
}

// Consume on the main task: queue receive synchronizes access to reply/history.
const char *aiPollReply(const char **text) {
  bool ok=false;
  if (!chatBusy || xQueueReceive(completed, &ok, 0) != pdTRUE) return nullptr;
  chatBusy=false;
  retryAt=ok?0:millis()+RETRY_BACKOFF_MS;
  *text=pendingText;
  return ok?outBuf:nullptr;
}
bool aiBusy() { return chatBusy; }
