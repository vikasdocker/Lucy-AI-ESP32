#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

static const char *AP_SSID = "Lucy_AI";
static const char *AP_PASSWORD = "LucyAI123"; // Change before public use.
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GW(192, 168, 4, 1);
static const IPAddress AP_MASK(255, 255, 255, 0);

WebServer server(80);

static const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#f7f8fc"><title>Lucy AI</title>
<style>
:root{font-family:Inter,system-ui,-apple-system,sans-serif;color:#182230;background:#f7f8fc}
*{box-sizing:border-box}body{margin:0;min-height:100vh;display:flex;flex-direction:column}
header{padding:16px 18px;background:#fff;border-bottom:1px solid #e8ebf1;display:flex;align-items:center;gap:12px;position:sticky;top:0}
.logo{width:38px;height:38px;border-radius:13px;background:#e8f5ed;display:grid;place-items:center;font-size:21px}
h1{font-size:18px;margin:0}small{color:#778294}
main{width:min(820px,100%);margin:0 auto;padding:18px 14px 130px;flex:1}
#messages{display:flex;flex-direction:column;gap:12px}
.msg{max-width:88%;padding:12px 15px;border-radius:18px;line-height:1.48;white-space:pre-wrap;overflow-wrap:anywhere;font-size:15px}
.user{align-self:flex-end;background:#dceaff;border-bottom-right-radius:5px}
.assistant{align-self:flex-start;background:#fff;border:1px solid #e9edf3;border-bottom-left-radius:5px}
.system{align-self:center;color:#758195;font-size:12px;text-align:center}
.composer{position:fixed;bottom:0;left:0;right:0;padding:12px 12px max(12px,env(safe-area-inset-bottom));background:#ffffffed;border-top:1px solid #e8ebf1;backdrop-filter:blur(12px)}
form{display:flex;gap:8px;max-width:820px;margin:auto}
input,textarea{font:inherit}#prompt{flex:1;min-width:0;border:1px solid #dce2eb;border-radius:22px;padding:13px 16px;outline:none;background:#f9fafc}
button{border:0;border-radius:22px;padding:0 19px;background:#246bce;color:white;font-weight:600;font-size:14px}
button:disabled{opacity:.5}
.tools{display:flex;justify-content:center;gap:18px;padding-top:9px;font-size:12px}
.tools a{color:#63748a;text-decoration:none}
#settings{display:none;background:white;border:1px solid #e4e9f0;border-radius:14px;padding:14px;margin-bottom:15px}
#endpoint{width:100%;border:1px solid #dce2eb;border-radius:9px;padding:10px;margin:8px 0}
#save{padding:10px 14px;border-radius:10px}
</style></head><body>
<header><div class="logo">🤖</div><div><h1>Lucy AI</h1><small>ESP32 personal chatbot</small></div></header>
<main><section id="settings"><b>AI connection settings</b><p><small>Paste your secure Cloudflare Worker URL. It is saved only in this browser.</small></p><input id="endpoint" placeholder="https://your-worker.workers.dev"><button id="save" type="button">Save connection</button></section><div id="messages"></div></main>
<div class="composer"><form id="form"><input id="prompt" autocomplete="off" placeholder="Message Lucy..." maxlength="2000"><button id="send">Send</button></form><div class="tools"><a href="#" id="settingsLink">AI settings</a><a href="/update">Firmware update</a><a href="#" id="clear">Clear chat</a></div></div>
<script>
const $=id=>document.getElementById(id), messages=[], box=$("messages");
let busy=false;
function add(text,role){const d=document.createElement("div");d.className="msg "+role;d.textContent=text;box.appendChild(d);window.scrollTo(0,document.body.scrollHeight);return d}
function endpoint(){return localStorage.getItem("lucy_endpoint")||""}
$("endpoint").value=endpoint();
$("settingsLink").onclick=e=>{e.preventDefault();$("settings").style.display=$("settings").style.display==="block"?"none":"block"};
$("save").onclick=()=>{const u=$("endpoint").value.trim();try{const x=new URL(u);if(x.protocol!=="https:")throw 0;localStorage.setItem("lucy_endpoint",u);add("AI connection URL saved. Try sending a message.","system")}catch{add("Please enter a valid HTTPS Worker URL.","system")}};
$("clear").onclick=e=>{e.preventDefault();messages.length=0;box.innerHTML="";add("Chat cleared. How can I help?","assistant")};
add("Hi! I'm Lucy. Add your secure AI Worker URL in AI settings, then ask me anything.","assistant");
$("form").onsubmit=async e=>{
 e.preventDefault();if(busy)return;const text=$("prompt").value.trim();if(!text)return;
 const url=endpoint();if(!url){$("settings").style.display="block";add("First add your Cloudflare Worker URL in AI settings.","system");return}
 $("prompt").value="";add(text,"user");messages.push({role:"user",content:text});busy=true;$("send").disabled=true;
 const wait=add("Lucy is thinking…","assistant");
 try{
  const r=await fetch(url,{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({messages:messages.slice(-16)})});
  const data=await r.json();if(!r.ok||typeof data.reply!=="string")throw new Error(data.error||"Request failed ("+r.status+")");
  wait.textContent=data.reply;messages.push({role:"assistant",content:data.reply});
 }catch(err){wait.textContent="I couldn't reach the AI service: "+err.message+". Check mobile data and your Worker URL.";messages.pop()}
 finally{busy=false;$("send").disabled=false;$("prompt").focus()}
};
</script></body></html>
)HTML";

static const char UPDATE_PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lucy AI Firmware Update</title><style>body{font:16px system-ui;margin:24px;background:#f7f8fc;color:#182230}
main{max-width:520px;margin:40px auto;background:white;padding:22px;border-radius:18px}
button{background:#246bce;color:white;border:0;border-radius:10px;padding:12px 18px}
input{max-width:100%;margin:18px 0}a{color:#246bce}</style></head>
<body><main><h2>Lucy AI Firmware Update</h2><p>Select a compatible <b>.bin</b> firmware built for ESP32 DevKit (ESP32-WROOM-32), then upload.</p>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="firmware" accept=".bin" required><br><button type="submit">Install firmware</button></form>
<p><small>Keep the board powered and do not close this page during upload.</small></p><a href="/">Back to Lucy AI</a></main></body></html>
)HTML";

void handleRoot() { server.send_P(200, "text/html; charset=utf-8", PAGE); }
void handleUpdatePage() { server.send_P(200, "text/html; charset=utf-8", UPDATE_PAGE); }

void handleUpdateUpload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("OTA start: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.printf("OTA success: %u bytes\n", upload.totalSize);
    else Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_GW, AP_MASK);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD, 6, false, 4)) {
    Serial.println("Wi-Fi AP start failed"); return;
  }
  server.on("/", HTTP_GET, handleRoot);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST,
    []() {
      bool ok = !Update.hasError();
      server.send(200, "text/plain", ok ? "Update successful. Restarting..." : "Update failed. Keep the board powered and retry.");
      if (ok) { delay(700); ESP.restart(); }
    },
    handleUpdateUpload
  );
  server.onNotFound([](){ server.send(404, "text/plain", "Not found. Open http://192.168.4.1/"); });
  server.begin();
  Serial.printf("Lucy AI AP ready at http://%s\n", WiFi.softAPIP().toString().c_str());
}

void loop() { server.handleClient(); delay(2); }
