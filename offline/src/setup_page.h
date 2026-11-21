#pragma once
static const char SETUP_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lucy settings</title><style>
body{font:16px system-ui;background:#10151b;color:#f1f5f9;max-width:540px;margin:auto;padding:24px}
h1{font-size:26px}fieldset{border:1px solid #344252;border-radius:16px;margin:20px 0;padding:18px}
legend{color:#a8e6cf}label{display:block;margin:12px 0}input:not([type=checkbox]),select{display:block;box-sizing:border-box;width:100%;padding:10px;margin:6px 0;background:#202c38;color:inherit;border:1px solid #506070;border-radius:7px}
button{padding:14px 24px;border:0;border-radius:12px;background:#a8e6cf;font-size:16px}p{line-height:1.5}a{color:#a8e6cf}small{color:#bac5d2}
</style><h1>Lucy settings</h1><p id="status" role="status">Loading settings...</p>
<form id="settings"><fieldset><legend>Display &amp; sound</legend>
<label>Brightness <input name="brightness" type="range" min="10" max="100" step="10"></label>
<label>Language <select name="language"><option value="0">English</option><option value="1">Hindi (romanized)</option><option value="2">Marathi (romanized)</option></select></label>
<label>Speaker volume <input name="volume" type="range" min="0" max="100" step="10"></label>
</fieldset><fieldset><legend>Wi-Fi</legend>
<label><input type="checkbox" name="wifi_enabled"> Connect to router</label>
<label>Network name <input name="wifi_ssid" maxlength="32" autocomplete="off"></label>
<label>Password <input name="wifi_password" type="password" maxlength="64" autocomplete="new-password" placeholder="Leave blank to keep saved password"></label>
<label><input type="checkbox" name="wifi_open"> This router has no password</label><small>2.4 GHz networks. Use the exact name, including spaces.</small>
</fieldset><fieldset><legend>Lucy's access point</legend>
<label><input type="checkbox" name="ap_enabled"> Enable setup access point</label>
<label>Name <input name="ap_ssid" maxlength="32" required></label>
<label>Password <input name="ap_password" type="password" minlength="8" maxlength="63" autocomplete="new-password" placeholder="Leave blank to keep current password"></label>
<label><input type="checkbox" name="ap_open"> Use an open setup network</label>
<small>Reconnect after changing these settings. The physical button can enable the access point again.</small>
</fieldset><fieldset><legend>Clipbot speech (ESP speaker)</legend>
<label><input type="checkbox" name="tts_enabled"> Speak text replies through MAX98357A</label>
<label>TTS server URL (local HTTP or remote HTTPS) <input name="tts_url" maxlength="95" placeholder="https://voice.example.com"></label>
<label>Remote TTS access token <input name="tts_token" type="password" maxlength="95" placeholder="Leave blank to keep saved token"></label>
<small>Use a local TTS service or a remote HTTPS service with a certificate chaining to ISRG Root X1. Requires 16 kHz mono signed 16-bit PCM and Content-Length. Remote access should require a token. This is independent of the AI model URL.</small>
</fieldset><button>Save settings</button></form><p><a href="/">Back to Lucy</a></p>
<script>
const form=document.querySelector('form'), status=document.querySelector('#status');
form.elements.ap_password.oninput=()=>{if(form.elements.ap_password.value)form.elements.ap_open.checked=false};
form.elements.wifi_password.oninput=()=>{if(form.elements.wifi_password.value)form.elements.wifi_open.checked=false};
fetch('/api/settings').then(r=>{if(!r.ok)throw Error('Join Lucy\'s setup Wi-Fi to edit settings.');return r.json()}).then(v=>{for(const [k,x] of Object.entries(v)){const e=form.elements[k];if(e){if(e.type==='checkbox')e.checked=x;else e.value=x}}status.textContent='Ready. Passwords and tokens are kept private.'}).catch(e=>status.textContent=e.message);
form.onsubmit=async e=>{e.preventDefault();const body=new URLSearchParams(new FormData(form));for(const el of form.querySelectorAll('[type=checkbox]'))body.set(el.name,el.checked?'1':'0');try{const r=await fetch('/api/settings',{method:'POST',headers:{'X-Lucy-Setup':'1'},body});status.textContent=await r.text();}catch(e){status.textContent='Connection changed. Rejoin Lucy\'s access point and reload.'}};
</script></html>)HTML";
