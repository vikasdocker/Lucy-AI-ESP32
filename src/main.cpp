#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

static const char *AP_SSID = "Edsoft-ESP32-OTA";
static const char *AP_PASSWORD = "LucyAI123"; // Reconnect your phone using this password.
static const char *MDNS_HOST = "edsoft";

AsyncWebServer server(80);
volatile bool rebootPending = false;
bool mdnsStarted = false;

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
button:disabled{opacity:.5}
.tools{display:flex;justify-content:center;gap:20px;padding-top:10px;font-size:12px}
.tools a{color:#63748a;text-decoration:none}
#settings{display:none;background:white;border:1px solid #e4e9f0;border-radius:14px;padding:14px;margin-bottom:14px}
#endpoint{width:100%;border:1px solid #dce2eb;border-radius:9px;padding:11px;margin:8px 0;font:inherit}
#save{padding:10px 14px;border-radius:10px}
.status{font-size:12px;color:#6b7788;text-align:center;margin:8px}
</style></head><body>
<header><div class="logo">🤖</div><div><h1>Lucy AI</h1><small>ESP32 personal chatbot</small></div></header>
<main>
<section id="settings">
<b>AI connection settings</b>
<p><small>Paste your HTTPS Cloudflare Worker URL. The OpenAI key stays on the server, not on this device.</small></p>
<input id="endpoint" type="url" placeholder="https://your-worker.workers.dev" autocomplete="url">
<button id="save" type="button">Save connection</button>
</section>
<div id="status" class="status">ESP32 hotspot is active</div>
<div id="messages"></div>
</main>
<div class="composer">
<form id="form"><input id="prompt" autocomplete="off" placeholder="Message Lucy..." maxlength="2000"><button id="send" type="submit">Send</button></form>
<div class="tools"><a href="#" id="settingsLink">AI settings</a><a href="/update">Firmware update</a><a href="#" id="clear">Clear chat</a></div>
</div>
<script>
const $=id=>document.getElementById(id), messages=[], box=$("messages");
let busy=false;
function add(text,role){const d=document.createElement("div");d.className="msg "+role;d.textContent=text;box.appendChild(d);window.scrollTo(0,document.body.scrollHeight);return d}
function getEndpoint(){return localStorage.getItem("lucy_endpoint")||""}
$("endpoint").value=getEndpoint();
$("settingsLink").onclick=e=>{e.preventDefault();$("settings").style.display=$("settings").style.display==="block"?"none":"block"};
$("save").onclick=()=>{
 const value=$("endpoint").value.trim();
 try{const u=new URL(value);if(u.protocol!=="https:")throw new Error();localStorage.setItem("lucy_endpoint",value);$("settings").style.display="none";add("AI server saved. You can start chatting now.","system")}
 catch{add("Enter a valid HTTPS Worker URL.","system")}
};
$("clear").onclick=e=>{e.preventDefault();messages.length=0;box.innerHTML="";add("Chat cleared. How can I help?","assistant")};
add("Hello! I'm Lucy. Add your secure AI Worker URL in AI settings to begin.","assistant");
$("form").onsubmit=async e=>{
 e.preventDefault();if(busy)return;
 const text=$("prompt").value.trim();if(!text)return;
 const url=getEndpoint();
 if(!url){$("settings").style.display="block";add("Please configure your AI Worker URL in AI settings first.","system");return}
 $("prompt").value="";add(text,"user");messages.push({role:"user",content:text});
 busy=true;$("send").disabled=true;const waiting=add("Lucy is thinking…","assistant");
 const controller=new AbortController();const timeout=setTimeout(()=>controller.abort(),45000);
 try{
  const response=await fetch(url,{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({messages:messages.slice(-16)}),signal:controller.signal});
  const data=await response.json();
  if(!response.ok||typeof data.reply!=="string")throw new Error(data.error||"AI request failed ("+response.status+")");
  waiting.textContent=data.reply;messages.push({role:"assistant",content:data.reply});
 }catch(err){
  waiting.textContent=err.name==="AbortError"?"The AI request timed out. Please try again.":"I couldn't reach the AI service: "+err.message;
  if(messages.length&&messages[messages.length-1].role==="user")messages.pop();
 }finally{clearTimeout(timeout);busy=false;$("send").disabled=false;$("prompt").focus()}
};
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
<p><a href="/">Back to Lucy AI</a></p></main>
<script>
document.getElementById("upload").addEventListener("submit",e=>{
 e.preventDefault();const file=document.getElementById("file").files[0];if(!file)return;
 const xhr=new XMLHttpRequest();xhr.open("POST","/update");
 const fill=document.getElementById("fill"),msg=document.getElementById("message"),btn=document.getElementById("btn");
 btn.disabled=true;msg.textContent="Uploading firmware…";
 xhr.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(e.loaded/e.total*100);fill.style.width=p+"%";fill.textContent=p+"%"}};
 xhr.onload=()=>{if(xhr.status===200){fill.textContent="Upload complete";msg.textContent="Firmware accepted. ESP32 is restarting. Reconnect to Edsoft-ESP32-OTA in about 10 seconds.";setTimeout(()=>location.href="/",12000)}else{btn.disabled=false;msg.textContent="Upload failed: "+xhr.responseText}};
 xhr.onerror=()=>{btn.disabled=false;msg.textContent="Network error during upload. Check the ESP32 power and try again."};
 const fd=new FormData();fd.append("update",file,file.name);xhr.send(fd);
});
</script></body></html>
)HTML";

void startPermanentAccessPoint() {
  WiFi.mode(WIFI_AP_STA);
  delay(100);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD, 6, false, 4)) {
    Serial.println("ERROR: permanent AP start failed");
  } else {
    Serial.print("Permanent AP: "); Serial.println(AP_SSID);
    Serial.print("AP address: "); Serial.println(WiFi.softAPIP());
  }
}

void startMdnsIfConnected() {
  if (WiFi.status() == WL_CONNECTED && !mdnsStarted) {
    if (MDNS.begin(MDNS_HOST)) {
      MDNS.addService("http", "tcp", 80);
      mdnsStarted = true;
      Serial.println("mDNS available at http://edsoft.local/");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  // WiFiManager provides the initial router-credential portal if needed.
  WiFi.mode(WIFI_AP_STA);
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180);
  bool connected = wifiManager.autoConnect(AP_SSID, AP_PASSWORD);

  // Keep the local AP alive whether or not a router was configured.
  startPermanentAccessPoint();

  if (connected && WiFi.status() == WL_CONNECTED) {
    Serial.print("Router IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("Router not connected. Local AP remains available.");
  }
  startMdnsIfConnected();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html; charset=utf-8", index_html);
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html; charset=utf-8", update_html);
  });

  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (Update.hasError()) {
        request->send(500, "text/plain", "Firmware update failed. Check serial diagnostics and retry.");
      } else {
        request->send(200, "text/plain", "Firmware update successful. Restarting.");
        rebootPending = true;
      }
    },
    [](AsyncWebServerRequest *request, String filename, size_t index,
       uint8_t *data, size_t len, bool final) {
      if (index == 0) {
        Serial.printf("OTA Start: %s\n", filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
          Update.printError(Serial);
        }
      }
      if (!Update.hasError() && len) {
        if (Update.write(data, len) != len) Update.printError(Serial);
      }
      if (final) {
        if (Update.end(true)) {
          Serial.printf("OTA Success: %u bytes\n", index + len);
        } else {
          Update.printError(Serial);
        }
      }
    }
  );

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found. Open http://192.168.4.1/");
  });
  server.begin();
  Serial.println("Lucy AI web server started.");
}

void loop() {
  startMdnsIfConnected();
  if (rebootPending) {
    delay(800);
    ESP.restart();
  }
  delay(20);
}
