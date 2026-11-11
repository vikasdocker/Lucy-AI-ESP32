#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <stdarg.h>

static const char *AP_SSID = "Edsoft-ESP32-OTA";
static const char *AP_PASSWORD = "Intel2023#";
static const char *MDNS_HOST = "edsoft";
static const uint8_t DNS_PORT = 53;
static const size_t LOG_CAPACITY = 80;
static const size_t LOG_LENGTH = 160;

AsyncWebServer server(80);
AsyncEventSource events("/events");
DNSServer dnsServer;
Preferences preferences;

volatile bool rebootPending = false;
bool mdnsStarted = false;
bool dnsPortalActive = false;
bool connectRequested = false;
String pendingSsid;
String pendingPassword;
String savedSsid;
String savedPassword;
wl_status_t lastWifiStatus = WL_IDLE_STATUS;

char logEntries[LOG_CAPACITY][LOG_LENGTH];
size_t logHead = 0;
size_t logCount = 0;
portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

void logLine(const char *format, ...) {
  char message[LOG_LENGTH];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  char timestamped[LOG_LENGTH];
  snprintf(timestamped, sizeof(timestamped), "[%lu ms] %s",
           static_cast<unsigned long>(millis()), message);
  Serial.println(timestamped);

  portENTER_CRITICAL(&logMux);
  strncpy(logEntries[logHead], timestamped, LOG_LENGTH - 1);
  logEntries[logHead][LOG_LENGTH - 1] = '\0';
  logHead = (logHead + 1) % LOG_CAPACITY;
  if (logCount < LOG_CAPACITY) logCount++;
  portEXIT_CRITICAL(&logMux);

  events.send(timestamped, "log", millis());
}

String jsonEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if ((uint8_t)c >= 0x20) out += c;
  }
  return out;
}

String wifiStatusJson() {
  String json = "{";
  json += "\"connected\":";
  json += (WiFi.status() == WL_CONNECTED ? "true" : "false");
  json += ",\"ssid\":\"" + jsonEscape(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String("")) + "\"";
  json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += ",\"rssi\":";
  json += (WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) : String("0"));
  json += ",\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\"";
  json += ",\"ap_ssid\":\"" + String(AP_SSID) + "\"";
  json += ",\"state\":\"";
  json += (WiFi.status() == WL_CONNECTED ? "connected" : (connectRequested ? "connecting" : "disconnected"));
  json += "\"}";
  return json;
}

void startAccessPoint() {
  WiFi.mode(WIFI_AP_STA);
  delay(100);
  if (WiFi.softAP(AP_SSID, AP_PASSWORD, 6, false, 4)) {
    logLine("AP started: %s", AP_SSID);
    logLine("AP address: %s", WiFi.softAPIP().toString().c_str());
  } else {
    logLine("ERROR: AP start failed");
  }
}

void startCaptiveDns() {
  if (!dnsPortalActive) {
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    dnsPortalActive = true;
    logLine("Captive DNS portal enabled");
  }
}

void stopCaptiveDns() {
  if (dnsPortalActive) {
    dnsServer.stop();
    dnsPortalActive = false;
    logLine("Captive DNS portal disabled");
  }
}

void startMdnsIfConnected() {
  if (WiFi.status() == WL_CONNECTED && !mdnsStarted) {
    if (MDNS.begin(MDNS_HOST)) {
      MDNS.addService("http", "tcp", 80);
      mdnsStarted = true;
      logLine("mDNS available at http://edsoft.local/");
    }
  }
}

static const char index_html[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#f7f8fc"><title>Lucy AI</title>
<style>
:root{font-family:system-ui,-apple-system,sans-serif;color:#182230;background:#f7f8fc}
*{box-sizing:border-box}body{margin:0;min-height:100vh;display:flex;flex-direction:column}
header{padding:15px 17px;background:#fff;border-bottom:1px solid #e8ebf1;display:flex;align-items:center;gap:12px;position:sticky;top:0}
.logo{width:40px;height:40px;border-radius:13px;background:#e8f5ed;display:grid;place-items:center;font-size:22px}
h1{font-size:18px;margin:0}small{color:#778294}
main{width:min(820px,100%);margin:0 auto;padding:16px 13px 145px;flex:1}
#messages{display:flex;flex-direction:column;gap:11px}
.msg{max-width:89%;padding:12px 15px;border-radius:18px;line-height:1.5;white-space:pre-wrap;overflow-wrap:anywhere;font-size:15px}
.user{align-self:flex-end;background:#dceaff;border-bottom-right-radius:5px}
.assistant{align-self:flex-start;background:#fff;border:1px solid #e9edf3;border-bottom-left-radius:5px}
.system{align-self:center;color:#758195;font-size:12px;text-align:center}
.composer{position:fixed;bottom:0;left:0;right:0;padding:11px 11px max(11px,env(safe-area-inset-bottom));background:#ffffffed;border-top:1px solid #e8ebf1;backdrop-filter:blur(12px)}
form{display:flex;gap:8px;max-width:820px;margin:auto}
#prompt{flex:1;min-width:0;border:1px solid #dce2eb;border-radius:22px;padding:13px 15px;outline:none;background:#f9fafc;font:inherit}
button{border:0;border-radius:22px;padding:0 19px;background:#246bce;color:white;font-weight:600;font-size:14px}
button:disabled{opacity:.5}.tools{display:flex;justify-content:center;gap:18px;padding-top:10px;font-size:12px;flex-wrap:wrap}
.tools a{color:#63748a;text-decoration:none}
#settings{display:none;background:white;border:1px solid #e4e9f0;border-radius:14px;padding:14px;margin-bottom:14px}
#endpoint{width:100%;border:1px solid #dce2eb;border-radius:9px;padding:11px;margin:8px 0;font:inherit}
#save{padding:10px 14px;border-radius:10px}.status{font-size:12px;color:#6b7788;text-align:center;margin:8px}
</style></head><body>
<header><div class="logo">🤖</div><div><h1>Lucy AI</h1><small>ESP32 personal chatbot</small></div></header>
<main>
<section id="settings"><b>AI connection settings</b>
<p><small>Paste your HTTPS Cloudflare Worker URL. The API key stays on the server.</small></p>
<input id="endpoint" type="url" placeholder="https://your-worker.workers.dev" autocomplete="url">
<button id="save" type="button">Save connection</button></section>
<div id="status" class="status">ESP32 hotspot is active</div><div id="messages"></div>
</main>
<div class="composer"><form id="form"><input id="prompt" autocomplete="off" placeholder="Message Lucy..." maxlength="2000"><button id="send" type="submit">Send</button></form>
<div class="tools"><a href="#" id="settingsLink">AI settings</a><a href="/wifi">Wi-Fi settings</a><a href="/logs">System logs</a><a href="/info">Diagnostics</a><a href="/update">Firmware update</a><a href="#" id="clear">Clear chat</a></div></div>
<script>
const $=id=>document.getElementById(id),messages=[],box=$("messages");let busy=false;
function add(text,role){const d=document.createElement("div");d.className="msg "+role;d.textContent=text;box.appendChild(d);window.scrollTo(0,document.body.scrollHeight);return d}
function getEndpoint(){return localStorage.getItem("lucy_endpoint")||""}
async function clientLog(level,message){try{await fetch("/api/client-log",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams({level,message})})}catch(e){}}
$("endpoint").value=getEndpoint();
$("settingsLink").onclick=e=>{e.preventDefault();$("settings").style.display=$("settings").style.display==="block"?"none":"block"};
$("save").onclick=()=>{const value=$("endpoint").value.trim();try{const u=new URL(value);if(u.protocol!=="https:"||value.includes("https://https://"))throw new Error();localStorage.setItem("lucy_endpoint",value);$("settings").style.display="none";add("AI server saved. You can start chatting now.","system");clientLog("INFO","AI Worker URL saved in browser")}catch{add("Enter one valid HTTPS Worker URL (only one https://).","system")}};
$("clear").onclick=e=>{e.preventDefault();messages.length=0;box.innerHTML="";add("Chat cleared. How can I help?","assistant")};
add("Hello! I'm Lucy. Configure your AI Worker URL in AI settings to begin.","assistant");
$("form").onsubmit=async e=>{e.preventDefault();if(busy)return;const text=$("prompt").value.trim();if(!text)return;const url=getEndpoint();if(!url){$("settings").style.display="block";add("Please configure your AI Worker URL in AI settings first.","system");return}
$("prompt").value="";add(text,"user");messages.push({role:"user",content:text});busy=true;$("send").disabled=true;const waiting=add("Lucy is thinking…","assistant");const controller=new AbortController();const timeout=setTimeout(()=>controller.abort(),45000);
clientLog("INFO","AI request started");
try{const response=await fetch(url,{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({messages:messages.slice(-16)}),signal:controller.signal});clientLog("INFO","AI Worker HTTP status "+response.status);const data=await response.json();if(!response.ok||typeof data.reply!=="string")throw new Error(data.error||"AI request failed ("+response.status+")");waiting.textContent=data.reply;messages.push({role:"assistant",content:data.reply});clientLog("INFO","AI response received successfully")}
catch(err){waiting.textContent=err.name==="AbortError"?"The AI request timed out. Please try again.":"I couldn't reach the AI service: "+err.message;clientLog("ERROR",err.name==="AbortError"?"AI request timed out":err.message);if(messages.length&&messages[messages.length-1].role==="user")messages.pop()}
finally{clearTimeout(timeout);busy=false;$("send").disabled=false;$("prompt").focus()}};
</script></body></html>
)HTML";

static const char wifi_html[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>Lucy AI Wi-Fi</title>
<style>
body{font:16px system-ui;margin:0;padding:18px;background:#f7f8fc;color:#182230}main{max-width:600px;margin:20px auto;background:white;padding:20px;border-radius:18px}
button{background:#246bce;color:white;border:0;border-radius:10px;padding:11px 15px;font:inherit;margin:5px 4px 5px 0}
input{width:100%;box-sizing:border-box;padding:12px;border:1px solid #dce2eb;border-radius:9px;margin:7px 0;font:inherit}
.network{padding:11px;border:1px solid #e4e9f0;border-radius:9px;margin:7px 0;cursor:pointer}.muted{color:#667085;font-size:13px}
a{color:#246bce}#status{padding:10px;background:#f1f5fb;border-radius:9px;margin:12px 0}
</style></head><body><main><h2>Lucy AI — Wi-Fi settings</h2>
<div id="status">Checking connection…</div><button onclick="scan()">Scan networks</button><div id="networks"></div>
<form id="form"><label>Wi-Fi name (SSID)</label><input id="ssid" name="ssid" required maxlength="32" autocomplete="off">
<label>Wi-Fi password</label><input id="password" name="password" type="password" maxlength="63" autocomplete="new-password">
<button type="submit">Save and connect</button></form>
<p class="muted">The ESP32 setup hotspot remains available while connecting. Credentials are stored in ESP32 NVS.</p>
<p><a href="/">Lucy AI</a> · <a href="/logs">System logs</a> · <a href="/info">Diagnostics</a></p></main>
<script>
async function status(){try{const r=await fetch("/api/wifi/status");const d=await r.json();document.getElementById("status").textContent=d.connected?"Connected to "+d.ssid+" · "+d.ip+" · RSSI "+d.rssi+" dBm":"Not connected · Setup AP: "+d.ap_ssid+" ("+d.ap_ip+") · "+d.state}catch(e){document.getElementById("status").textContent="Unable to read Wi-Fi status"}}
async function scan(){const box=document.getElementById("networks");box.textContent="Scanning nearby networks…";try{await fetch("/api/wifi/scan",{method:"POST"});let d;for(let i=0;i<15;i++){await new Promise(r=>setTimeout(r,1000));const r=await fetch("/api/wifi/scan/results");d=await r.json();if(d.state!=="running")break}box.innerHTML="";if(!d||!d.networks||!d.networks.length){box.textContent="No networks found. Enter SSID manually.";return}d.networks.forEach(n=>{const el=document.createElement("div");el.className="network";el.textContent=n.ssid+" · "+n.rssi+" dBm"+(n.open?" · Open":" · Secured");el.onclick=()=>document.getElementById("ssid").value=n.ssid;box.appendChild(el)})}catch(e){box.textContent="Scan failed: "+e.message}}
document.getElementById("form").onsubmit=async e=>{e.preventDefault();const body=new URLSearchParams({ssid:document.getElementById("ssid").value,password:document.getElementById("password").value});const r=await fetch("/wifisave",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body});const d=await r.json();document.getElementById("status").textContent=d.message||"Credentials saved";setTimeout(status,1500)};
status();setInterval(status,4000);
</script></body></html>
)HTML";

static const char logs_html[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>Lucy AI Logs</title>
<style>
body{font:14px system-ui;margin:0;padding:15px;background:#f7f8fc;color:#182230}main{max-width:900px;margin:15px auto;background:white;padding:16px;border-radius:16px}
header{display:flex;justify-content:space-between;align-items:center;gap:10px;flex-wrap:wrap}
button{background:#246bce;color:white;border:0;border-radius:9px;padding:9px 13px;font:inherit}
#log{background:#111827;color:#d1fae5;border-radius:10px;padding:12px;height:65vh;overflow:auto;font:12px/1.6 ui-monospace,monospace;white-space:pre-wrap;overflow-wrap:anywhere}
.muted{color:#667085;font-size:12px}a{color:#246bce}
</style></head><body><main><header><div><h2>Lucy AI — Live system logs</h2><div class="muted" id="state">Connecting to live log stream…</div></div>
<div><button onclick="clearView()">Clear view</button> <button onclick="downloadLogs()">Download</button></div></header>
<pre id="log"></pre><p><a href="/">Lucy AI</a> · <a href="/wifi">Wi-Fi settings</a> · <a href="/info">Diagnostics</a></p></main>
<script>
const box=document.getElementById("log");function add(s){box.textContent+=s+"\n";box.scrollTop=box.scrollHeight}
function clearView(){box.textContent=""}
async function downloadLogs(){const b=new Blob([box.textContent],{type:"text/plain"});const a=document.createElement("a");a.href=URL.createObjectURL(b);a.download="lucy-ai-logs.txt";a.click();URL.revokeObjectURL(a.href)}
fetch("/api/logs").then(r=>r.text()).then(t=>{box.textContent=t;box.scrollTop=box.scrollHeight}).catch(()=>{});
const es=new EventSource("/events");es.addEventListener("log",e=>add(e.data));es.onopen=()=>document.getElementById("state").textContent="Live · updates stream automatically";es.onerror=()=>document.getElementById("state").textContent="Live stream reconnecting…";
</script></body></html>
)HTML";

static const char update_html[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP Firmware Update</title><style>
body{font:16px system-ui;margin:0;padding:20px;background:#f7f8fc;color:#182230}
main{max-width:540px;margin:35px auto;background:white;padding:22px;border-radius:18px}
button{background:#246bce;color:white;border:0;border-radius:10px;padding:12px 18px;font:inherit}
input{max-width:100%;margin:18px 0}#bar{height:22px;background:#e7ebf1;border-radius:12px;overflow:hidden}
#fill{height:100%;width:0;background:#26965b;color:white;text-align:center;font-size:12px;line-height:22px}
a{color:#246bce}small{color:#667085}
</style></head><body><main><h2>ESP Firmware Update</h2>
<p>Select a compatible ESP32 application firmware <b>.bin</b> file.</p>
<form id="upload"><input id="file" type="file" accept=".bin" required><br><button id="btn">Update firmware</button></form>
<div id="bar"><div id="fill">0%</div></div><p id="message"><small>Keep the ESP32 powered during upload.</small></p>
<p><a href="/">Lucy AI</a> · <a href="/logs">System logs</a></p></main>
<script>
document.getElementById("upload").addEventListener("submit",e=>{e.preventDefault();const file=document.getElementById("file").files[0];if(!file)return;
const xhr=new XMLHttpRequest();xhr.open("POST","/update");const fill=document.getElementById("fill"),msg=document.getElementById("message"),btn=document.getElementById("btn");
btn.disabled=true;msg.textContent="Uploading firmware…";xhr.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(e.loaded/e.total*100);fill.style.width=p+"%";fill.textContent=p+"%"}};
xhr.onload=()=>{if(xhr.status===200){fill.textContent="Upload complete";msg.textContent="Firmware accepted. ESP32 is restarting. Reconnect to Edsoft-ESP32-OTA in about 10 seconds.";setTimeout(()=>location.href="/",12000)}else{btn.disabled=false;msg.textContent="Upload failed: "+xhr.responseText}};
xhr.onerror=()=>{btn.disabled=false;msg.textContent="Network error during upload. Check ESP32 power and try again."};
const fd=new FormData();fd.append("update",file,file.name);xhr.send(fd)});
</script></body></html>
)HTML";

void setup() {
  Serial.begin(115200);
  delay(300);
  logLine("Lucy AI booting");
  preferences.begin("lucy-wifi", false);
  savedSsid = preferences.getString("ssid", "");
  savedPassword = preferences.getString("password", "");

  startAccessPoint();
  if (savedSsid.length()) {
    logLine("Saved Wi-Fi found; connecting to %s", savedSsid.c_str());
    WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
    connectRequested = true;
  } else {
    logLine("No saved Wi-Fi credentials; open Wi-Fi settings to provision");
    startCaptiveDns();
  }

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    (void)info;
    logLine("Wi-Fi event: %d", static_cast<int>(event));
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html; charset=utf-8", index_html);
  });
  server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html; charset=utf-8", wifi_html);
  });
  server.on("/logs", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html; charset=utf-8", logs_html);
  });
  server.on("/info", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><title>Lucy ESP32</title><style>body{font:16px system-ui;padding:20px;line-height:1.55}a{color:#246bce}</style><h2>Lucy AI ESP32 diagnostics</h2><pre>";
    html += "Uptime: " + String(millis() / 1000) + " seconds\n";
    html += "Chip ID: " + String((uint32_t)ESP.getEfuseMac(), HEX) + "\n";
    html += "Flash: " + String(ESP.getFlashChipSize()) + " bytes\n";
    html += "Free heap: " + String(ESP.getFreeHeap()) + " bytes\n";
    html += "Minimum free heap: " + String(ESP.getMinFreeHeap()) + " bytes\n";
    html += "CPU: " + String(ESP.getCpuFreqMHz()) + " MHz\n";
    html += "Wi-Fi: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected") + "\n";
    html += "SSID: " + WiFi.SSID() + "\n";
    html += "Station IP: " + WiFi.localIP().toString() + "\n";
    html += "AP IP: " + WiFi.softAPIP().toString() + "\n";
    html += "</pre><p><a href='/'>Lucy AI</a> · <a href='/wifi'>Wi-Fi</a> · <a href='/logs'>Logs</a></p>";
    request->send(200, "text/html; charset=utf-8", html);
  });

  server.on("/api/wifi/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", wifiStatusJson());
  });
  server.on("/api/wifi/scan", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
      request->send(202, "application/json", "{\"state\":\"running\"}");
      return;
    }
    WiFi.scanDelete();
    WiFi.scanNetworks(true, true);
    logLine("Wi-Fi scan requested");
    request->send(202, "application/json", "{\"state\":\"running\"}");
  });
  server.on("/api/wifi/scan/results", HTTP_GET, [](AsyncWebServerRequest *request) {
    int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) {
      request->send(200, "application/json", "{\"state\":\"running\"}");
      return;
    }
    if (count < 0) {
      request->send(200, "application/json", "{\"state\":\"failed\",\"networks\":[]}");
      return;
    }
    String json = "{\"state\":\"complete\",\"networks\":[";
    for (int i = 0; i < count; i++) {
      if (i) json += ",";
      String ssid = WiFi.SSID(i);
      json += "{\"ssid\":\"" + jsonEscape(ssid) + "\",\"rssi\":" + String(WiFi.RSSI(i));
      json += ",\"open\":";
      json += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false");
      json += "}";
    }
    json += "]}";
    WiFi.scanDelete();
    request->send(200, "application/json", json);
  });
  server.on("/wifisave", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("ssid", true)) {
      request->send(400, "application/json", "{\"message\":\"SSID is required\"}");
      return;
    }
    String ssid = request->getParam("ssid", true)->value();
    String password = request->hasParam("password", true) ? request->getParam("password", true)->value() : "";
    ssid.trim();
    if (!ssid.length() || ssid.length() > 32 || password.length() > 63) {
      request->send(400, "application/json", "{\"message\":\"Invalid SSID or password length\"}");
      return;
    }
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    pendingSsid = ssid;
    pendingPassword = password;
    connectRequested = true;
    request->send(200, "application/json", "{\"message\":\"Credentials saved. ESP32 is connecting; watch the status below and the live logs.\"}");
    logLine("New Wi-Fi credentials saved for SSID: %s", ssid.c_str());
  });
  server.on("/api/client-log", HTTP_POST, [](AsyncWebServerRequest *request) {
    String level = request->hasParam("level", true) ? request->getParam("level", true)->value() : "INFO";
    String message = request->hasParam("message", true) ? request->getParam("message", true)->value() : "Client event";
    if (message.length() > 110) message = message.substring(0, 110);
    logLine("[BROWSER][%s] %s", level.c_str(), message.c_str());
    request->send(204);
  });
  server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *request) {
    String output;
    portENTER_CRITICAL(&logMux);
    size_t start = (logHead + LOG_CAPACITY - logCount) % LOG_CAPACITY;
    for (size_t i = 0; i < logCount; i++) {
      output += logEntries[(start + i) % LOG_CAPACITY];
      output += "\n";
    }
    portEXIT_CRITICAL(&logMux);
    request->send(200, "text/plain; charset=utf-8", output);
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html; charset=utf-8", update_html);
  });
  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (Update.hasError()) {
        logLine("OTA failed");
        request->send(500, "text/plain", "Firmware update failed. Check logs and retry.");
      } else {
        logLine("OTA upload successful; restarting");
        request->send(200, "text/plain", "Firmware update successful. Restarting.");
        rebootPending = true;
      }
    },
    [](AsyncWebServerRequest *request, String filename, size_t index,
       uint8_t *data, size_t len, bool final) {
      if (index == 0) {
        logLine("OTA started: %s", filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
          Update.printError(Serial);
          logLine("OTA begin failed");
        }
      }
      if (!Update.hasError() && len) {
        if (Update.write(data, len) != len) {
          Update.printError(Serial);
          logLine("OTA write failed");
        }
      }
      if (final) {
        if (Update.end(true)) logLine("OTA success: %u bytes", static_cast<unsigned>(index + len));
        else { Update.printError(Serial); logLine("OTA finalization failed"); }
      }
    }
  );

  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("/"); });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("/"); });
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("/"); });
  server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("/"); });
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  server.addHandler(&events);
  events.onConnect([](AsyncEventSourceClient *client) {
    client->send("Connected to Lucy AI live log stream", "log", millis());
  });
  server.begin();
  logLine("Lucy AI web server started on port 80");
  lastWifiStatus = WiFi.status();
}

void loop() {
  if (dnsPortalActive) dnsServer.processNextRequest();

  if (connectRequested) {
    connectRequested = false;
    String ssid = pendingSsid.length() ? pendingSsid : savedSsid;
    String password = pendingSsid.length() ? pendingPassword : savedPassword;
    pendingSsid = "";
    pendingPassword = "";
    if (ssid.length()) {
      logLine("Connecting to Wi-Fi SSID: %s", ssid.c_str());
      WiFi.disconnect(false, false);
      WiFi.begin(ssid.c_str(), password.c_str());
    }
  }

  wl_status_t currentStatus = WiFi.status();
  if (currentStatus != lastWifiStatus) {
    lastWifiStatus = currentStatus;
    if (currentStatus == WL_CONNECTED) {
      logLine("Wi-Fi connected: %s", WiFi.SSID().c_str());
      logLine("Station IP: %s", WiFi.localIP().toString().c_str());
      logLine("Signal strength: %d dBm", WiFi.RSSI());
      stopCaptiveDns();
      startMdnsIfConnected();
    } else {
      logLine("Wi-Fi status changed: %d", static_cast<int>(currentStatus));
    }
  }
  startMdnsIfConnected();

  if (rebootPending) {
    delay(800);
    ESP.restart();
  }
  delay(10);
}
