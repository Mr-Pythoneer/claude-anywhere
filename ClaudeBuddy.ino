/*
  Claude Buddy v4.0 — AtomS3U
  ─────────────────────────────────────────────────────────────────
  Plug into any computer → HID keyboard opens a terminal and runs:
    curl -s http://claudebuddy.local/run | python3
  The companion script starts a local code-execution server, then
  opens the browser to the chat UI.  Code blocks get a ▶ Run button.

  FALLBACK: serial terminal at 115200 baud (works on any UART device)

  LED    orange = connecting   blue = ready   rainbow = thinking
         green  = done         red  = error
  Button tap    → clear conversation
         hold 3s → reset WiFi (captive portal on next boot)
  ─────────────────────────────────────────────────────────────────
*/

#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "USB.h"
#include "USBHIDKeyboard.h"

USBHIDKeyboard Keyboard;

#define LED_PIN  35
#define BTN_PIN  41
CRGB leds[1];

WebServer server(80);

// ── Config ────────────────────────────────────────────────────────────
const char* CLAUDE_API_KEY = "PUT_UR_APIKEY_HERE";
const char* CLAUDE_MODEL   = "claude-sonnet-4-6";
const char* CLAUDE_URL     = "https://api.anthropic.com/v1/messages";
const char* SYSTEM_PROMPT  =
  "You are Claude Buddy, running on an M5Stack AtomS3U hardware device. "
  "The user has a companion app running locally that can execute code. "
  "When writing Python, Bash, or Node.js code, write complete working scripts — "
  "the user can click the Run button next to any code block to run it instantly on their computer. "
  "You can help with coding, automation, file management, questions, and anything else.";
const int MAX_TOKENS  = 8192;
const int MAX_HISTORY = 16;

// ── Conversation history ──────────────────────────────────────────────
String histRoles[MAX_HISTORY];
String histContent[MAX_HISTORY];
int    histSize = 0;

// ── LED state ─────────────────────────────────────────────────────────
volatile bool    ledThinking = false;
volatile uint8_t ledR = 255, ledG = 80, ledB = 0;

// ── Companion script served at GET /run ───────────────────────────────
// Python 3 — works on Mac and Linux (Windows: install Python from python.org)
const char COMPANION_PY[] PROGMEM = R"py(
#!/usr/bin/env python3
"""Claude Buddy companion — runs on your computer, lets Claude execute code."""
import http.server, subprocess, json, webbrowser, threading, sys, os, signal

PORT = 12345

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def _cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def do_OPTIONS(self):
        self.send_response(200); self._cors(); self.end_headers()

    def do_GET(self):
        if self.path == "/ping":
            self.send_response(200); self._cors(); self.end_headers()
            self.wfile.write(b"ok")

    def do_POST(self):
        if self.path != "/execute":
            self.send_response(404); self.end_headers(); return
        length = int(self.headers.get("Content-Length", 0))
        body = json.loads(self.rfile.read(length))
        code = body.get("code", "")
        lang = body.get("lang", "").lower().strip()

        runners = {
            "python": ["python3", "-c"],
            "python3": ["python3", "-c"],
            "py": ["python3", "-c"],
            "bash": ["bash", "-c"],
            "sh": ["bash", "-c"],
            "shell": ["bash", "-c"],
            "node": ["node", "-e"],
            "javascript": ["node", "-e"],
            "js": ["node", "-e"],
        }
        cmd = runners.get(lang, ["bash", "-c"])

        try:
            r = subprocess.run(cmd + [code], capture_output=True, text=True, timeout=30)
            out = {"stdout": r.stdout, "stderr": r.stderr, "code": r.returncode}
        except subprocess.TimeoutExpired:
            out = {"stdout": "", "stderr": "Timed out after 30s", "code": 1}
        except FileNotFoundError as e:
            out = {"stdout": "", "stderr": str(e), "code": 1}
        except Exception as e:
            out = {"stdout": "", "stderr": str(e), "code": 1}

        resp = json.dumps(out).encode()
        self.send_response(200)
        self._cors()
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", len(resp))
        self.end_headers()
        self.wfile.write(resp)

httpd = http.server.HTTPServer(("127.0.0.1", PORT), Handler)
threading.Thread(target=httpd.serve_forever, daemon=True).start()
print(f"Claude Buddy companion running on http://localhost:{PORT}")
print("Opening browser...")
webbrowser.open("http://claudebuddy.local")
print("Press Ctrl+C to stop.\n")
signal.signal(signal.SIGINT, lambda *_: (httpd.shutdown(), sys.exit(0)))
threading.Event().wait()
)py";

// ── Web chat UI ───────────────────────────────────────────────────────
const char INDEX_HTML[] PROGMEM = R"html(
<!DOCTYPE html><html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Claude Buddy</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#0d1117;color:#e6edf3;height:100dvh;display:flex;flex-direction:column}
header{padding:12px 20px;background:#161b22;border-bottom:1px solid #30363d;display:flex;align-items:center;gap:12px;flex-wrap:wrap}
.dot{width:9px;height:9px;border-radius:50%;background:#238636;flex-shrink:0;animation:blink 2s infinite}
@keyframes blink{0%,100%{opacity:1}50%{opacity:.3}}
h1{font-size:.95rem;font-weight:600;color:#8b949e;flex:1}
h1 span{color:#58a6ff}
#cstatus{font-size:.78rem;padding:3px 10px;border-radius:20px;background:#161b22;border:1px solid #30363d;color:#8b949e;white-space:nowrap}
#cstatus.on{color:#3fb950;border-color:#238636}
#chat{flex:1;overflow-y:auto;padding:20px;display:flex;flex-direction:column;gap:16px}
.bubble{max-width:85%;padding:12px 16px;border-radius:12px;line-height:1.6;font-size:.9rem;word-break:break-word}
.user{background:#1f6feb;align-self:flex-end;border-radius:12px 12px 2px 12px;white-space:pre-wrap}
.claude{background:#161b22;border:1px solid #30363d;align-self:flex-start;border-radius:12px 12px 12px 2px}
.thinking{color:#8b949e;font-style:italic}
.thinking::after{content:'...';animation:dots 1.2s steps(3,end) infinite}
@keyframes dots{0%{content:'.'}33%{content:'..'}66%{content:'...'}}
#form{display:flex;padding:14px;gap:10px;background:#161b22;border-top:1px solid #30363d}
#inp{flex:1;padding:10px 14px;border-radius:8px;border:1px solid #30363d;background:#0d1117;color:#e6edf3;font-size:.95rem;outline:none;resize:none;height:42px;max-height:160px}
#inp:focus{border-color:#58a6ff}
#sbtn{padding:10px 18px;border-radius:8px;border:none;background:#238636;color:#fff;font-weight:600;cursor:pointer}
#sbtn:disabled{opacity:.4;cursor:default}
#sbtn:hover:not(:disabled){background:#2ea043}
code{background:#0d1117;padding:2px 6px;border-radius:4px;font-family:'Fira Code',Consolas,monospace;font-size:.85rem}
.code-wrap{position:relative;margin:6px 0}
.run-btn{position:absolute;top:8px;right:8px;padding:3px 10px;border-radius:5px;border:none;background:#1f6feb;color:#fff;font-size:.75rem;cursor:pointer;font-weight:600;opacity:.9}
.run-btn:hover{background:#388bfd}
.run-btn:disabled{opacity:.4;cursor:default}
pre{background:#010409;border:1px solid #30363d;border-radius:8px;padding:14px 14px 14px 14px;overflow-x:auto}
pre code{background:none;padding:0;font-size:.83rem}
.out{background:#010409;border:1px solid #238636;border-top:none;border-radius:0 0 8px 8px;padding:8px 12px;font-family:monospace;font-size:.8rem;white-space:pre-wrap;color:#3fb950;max-height:200px;overflow-y:auto}
.out.err{border-color:#da3633;color:#f85149}
</style>
</head>
<body>
<header>
  <div class="dot"></div>
  <h1><span>Claude Buddy</span> &nbsp;·&nbsp; claude-sonnet-4-6</h1>
  <div id="cstatus">⚡ companion offline</div>
</header>
<div id="chat">
  <div class="bubble claude">Hi! I'm Claude Buddy. Ask me anything — I can write and <strong>run</strong> code on your computer, answer questions, help with projects, and more.<br><br>Install the companion to enable code execution: open a terminal and run<br><code>curl -s http://claudebuddy.local/run | python3</code></div>
</div>
<form id="form">
  <textarea id="inp" placeholder="Type a message… (Enter to send, Shift+Enter for newline)" rows="1"></textarea>
  <button id="sbtn" type="submit">Send</button>
</form>
<script>
const chat=document.getElementById('chat');
const inp=document.getElementById('inp');
const btn=document.getElementById('sbtn');
const cstatus=document.getElementById('cstatus');
let companion=false;

async function checkCompanion(){
  try{
    await fetch('http://localhost:12345/ping',{signal:AbortSignal.timeout(800)});
    if(!companion){companion=true;cstatus.textContent='⚡ companion online';cstatus.className='on';}
  }catch{
    if(companion){companion=false;cstatus.textContent='⚡ companion offline';cstatus.className='';}
  }
}
checkCompanion();setInterval(checkCompanion,3000);

function escHtml(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}

function fmt(s){
  s=s.replace(/```([\w]*)\r?\n?([\s\S]*?)```/g,(_, lang, c)=>{
    const code=escHtml(c.trim());
    const l=lang.trim()||'';
    const runnable=['python','python3','py','bash','sh','shell','node','js','javascript'].includes(l);
    const rb=runnable?`<button class="run-btn" data-lang="${l}" onclick="runCode(this)">&#9654; Run</button>`:'';
    return `<div class="code-wrap">${rb}<pre><code>${code}</code></pre></div>`;
  });
  s=s.replace(/`([^`\n]+)`/g,(_,c)=>'<code>'+escHtml(c)+'</code>');
  s=s.replace(/\n/g,'<br>');
  return s;
}

async function runCode(btn){
  if(!companion){alert('Companion not running.\nOpen a terminal and run:\ncurl -s http://claudebuddy.local/run | python3');return;}
  const code=btn.parentElement.querySelector('pre code').textContent;
  const lang=btn.dataset.lang;
  btn.disabled=true;btn.textContent='⏳';
  let outEl=btn.parentElement.querySelector('.out');
  if(!outEl){outEl=document.createElement('div');outEl.className='out';btn.parentElement.appendChild(outEl);}
  outEl.className='out';outEl.textContent='Running...';
  try{
    const r=await fetch('http://localhost:12345/execute',{
      method:'POST',headers:{'Content-Type':'application/json'},
      body:JSON.stringify({code,lang})
    });
    const d=await r.json();
    const out=(d.stdout||'')+(d.stderr?'\n[stderr]\n'+d.stderr:'');
    outEl.textContent=out.trim()||'(no output)';
    outEl.className=d.code===0?'out':'out err';
    btn.textContent=d.code===0?'&#9654; Run':'&#9654; Run';
  }catch(e){
    outEl.textContent='Error: '+e.message;outEl.className='out err';
    btn.textContent='&#9654; Run';
  }
  btn.disabled=false;chat.scrollTop=chat.scrollHeight;
}

function addMsg(text,cls,raw){
  const d=document.createElement('div');
  d.className='bubble '+cls;
  if(raw)d.innerHTML=text;else d.textContent=text;
  chat.appendChild(d);chat.scrollTop=chat.scrollHeight;
  return d;
}

inp.addEventListener('input',()=>{inp.style.height='42px';inp.style.height=inp.scrollHeight+'px'});
inp.addEventListener('keydown',e=>{
  if(e.key==='Enter'&&!e.shiftKey){e.preventDefault();document.getElementById('form').dispatchEvent(new Event('submit'));}
});
document.getElementById('form').addEventListener('submit',async e=>{
  e.preventDefault();
  const msg=inp.value.trim();
  if(!msg||btn.disabled)return;
  inp.value='';inp.style.height='42px';
  btn.disabled=true;
  addMsg(msg,'user',false);
  const thinking=addMsg('Thinking','claude thinking',false);
  try{
    const r=await fetch('/chat',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'msg='+encodeURIComponent(msg)});
    const d=await r.json();
    thinking.className='bubble claude';
    thinking.innerHTML=fmt(d.reply||d.error||'(empty)');
  }catch(err){
    thinking.className='bubble claude';
    thinking.textContent='Error: '+err.message;
  }
  btn.disabled=false;chat.scrollTop=chat.scrollHeight;inp.focus();
});
inp.focus();
</script>
</body></html>
)html";

// ── LED task (Core 1, high stack for FastLED) ─────────────────────────
void ledTask(void*) {
  uint8_t hue = 0;
  while (true) {
    if (ledThinking) {
      leds[0] = CHSV(hue, 255, 200);
      hue += 3;
    } else {
      leds[0] = CRGB(ledR, ledG, ledB);
    }
    FastLED.show();
    vTaskDelay(25 / portTICK_PERIOD_MS);
  }
}

void setLED(uint8_t r, uint8_t g, uint8_t b) {
  ledR = r; ledG = g; ledB = b;
  ledThinking = false;
}

// ── History ───────────────────────────────────────────────────────────
void addMessage(const String& role, const String& content) {
  if (histSize < MAX_HISTORY) {
    histRoles[histSize]   = role;
    histContent[histSize] = content;
    histSize++;
  } else {
    for (int i = 0; i < MAX_HISTORY - 1; i++) {
      histRoles[i]   = histRoles[i + 1];
      histContent[i] = histContent[i + 1];
    }
    histRoles[MAX_HISTORY - 1]   = role;
    histContent[MAX_HISTORY - 1] = content;
  }
}

void clearHistory() { histSize = 0; }

// ── Claude API ────────────────────────────────────────────────────────
String askClaude(const String& userMsg) {
  if (WiFi.status() != WL_CONNECTED) return F("[Not connected to WiFi]");

  addMessage("user", userMsg);

  JsonDocument req;
  req["model"]      = CLAUDE_MODEL;
  req["max_tokens"] = MAX_TOKENS;
  req["system"]     = SYSTEM_PROMPT;
  JsonArray msgs = req["messages"].to<JsonArray>();
  for (int i = 0; i < histSize; i++) {
    JsonObject m = msgs.add<JsonObject>();
    m["role"]    = histRoles[i];
    m["content"] = histContent[i];
  }
  String body;
  serializeJson(req, body);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, CLAUDE_URL);
  http.addHeader("Content-Type",      "application/json");
  http.addHeader("x-api-key",         CLAUDE_API_KEY);
  http.addHeader("anthropic-version", "2023-06-01");
  http.setTimeout(60000);

  int code = http.POST(body);
  if (code != 200) {
    String err = "[HTTP " + String(code) + "] " + http.getString();
    http.end(); histSize--;
    return err;
  }

  String raw = http.getString();
  http.end();

  JsonDocument res;
  if (deserializeJson(res, raw)) return F("[JSON parse error]");

  const char* apiErr = res["error"]["message"];
  if (apiErr) return String(F("[API error] ")) + apiErr;

  String reply = res["content"][0]["text"].as<String>();
  addMessage("assistant", reply);
  return reply;
}

// ── Web routes ────────────────────────────────────────────────────────
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleChat() {
  String msg = server.arg("msg");
  msg.trim();
  if (msg.isEmpty()) {
    server.send(400, "application/json", F("{\"error\":\"empty\"}"));
    return;
  }
  ledThinking = true;
  String reply = askClaude(msg);
  setLED(0, 200, 0); delay(300); setLED(0, 0, 200);
  JsonDocument doc;
  doc["reply"] = reply;
  String json; serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleClear() {
  clearHistory();
  server.send(200, "application/json", F("{\"ok\":true}"));
}

void handleRun() {
  server.send_P(200, "text/plain", COMPANION_PY);
}

// ── WiFi ──────────────────────────────────────────────────────────────
void connectWiFi() {
  setLED(255, 80, 0);
  Serial.println(F("[WiFi] Connecting..."));

  WiFiManager wm;
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(300);
  wm.setTitle("Claude Buddy Setup");
  wm.setDarkMode(true);

  wm.setAPCallback([](WiFiManager*) {
    Serial.println(F("[WiFi] No saved credentials."));
    Serial.println(F("[WiFi]   Connect to: Claude-Buddy-Setup"));
    Serial.println(F("[WiFi]   Then open:  http://192.168.4.1"));
    setLED(255, 80, 0);
  });

  if (!wm.autoConnect("Claude-Buddy-Setup")) {
    Serial.println(F("[WiFi] Failed. Restarting..."));
    setLED(255, 0, 0); delay(3000); ESP.restart();
  }

  Serial.print(F("[WiFi] Connected — IP: "));
  Serial.println(WiFi.localIP());
  setLED(0, 0, 200);
}

// ── Banner ────────────────────────────────────────────────────────────
void printBanner(const String& ip) {
  Serial.println();
  Serial.println(F("  ╔═══════════════════════════════════════╗"));
  Serial.println(F("  ║       CLAUDE BUDDY  v4.0              ║"));
  Serial.println(F("  ║    AtomS3U · claude-sonnet-4-6        ║"));
  Serial.println(F("  ╠═══════════════════════════════════════╣"));
  Serial.println(F("  ║  OPEN IN BROWSER (same WiFi):         ║"));
  Serial.print  (F("  ║    http://claudebuddy.local           ║\n"));
  Serial.print  (F("  ║    http://"));
  Serial.print(ip);
  for (int i = ip.length(); i < 28; i++) Serial.print(' ');
  Serial.println('║');
  Serial.println(F("  ╠═══════════════════════════════════════╣"));
  Serial.println(F("  ║  COMPANION (enables code execution):  ║"));
  Serial.println(F("  ║  curl -s http://claudebuddy.local/run ║"));
  Serial.println(F("  ║    | python3                          ║"));
  Serial.println(F("  ╠═══════════════════════════════════════╣"));
  Serial.println(F("  ║  LED   orange = connecting            ║"));
  Serial.println(F("  ║        blue   = ready                 ║"));
  Serial.println(F("  ║        rainbow= thinking              ║"));
  Serial.println(F("  ║        green  = done  · red = error   ║"));
  Serial.println(F("  ╠═══════════════════════════════════════╣"));
  Serial.println(F("  ║  BUTTON tap=clear  hold 3s=reset WiFi ║"));
  Serial.println(F("  ╠═══════════════════════════════════════╣"));
  Serial.println(F("  ║  No WiFi? SSID: Claude-Buddy-Setup    ║"));
  Serial.println(F("  ║           URL:  http://192.168.4.1    ║"));
  Serial.println(F("  ╚═══════════════════════════════════════╝"));
  Serial.println();
  Serial.println(F("  Or type here and press Enter."));
  Serial.println();
}

// ── HID: open terminal and run companion ─────────────────────────────
// Tries Mac → Windows → Linux in sequence. Each opens a terminal and
// types the curl command to download and run the companion script.
void autoLaunchCompanion() {
  delay(1500);
  const char* cmd = "curl -s http://claudebuddy.local/run | python3";

  // ── Mac: Cmd+Space → "terminal" → Enter → curl command ──────────
  Keyboard.press(KEY_LEFT_GUI); Keyboard.press(' ');
  delay(120); Keyboard.releaseAll(); delay(700);
  Keyboard.print("terminal");
  delay(300); Keyboard.press(KEY_RETURN); Keyboard.releaseAll();
  delay(2500);
  Keyboard.print(cmd);
  delay(100); Keyboard.press(KEY_RETURN); Keyboard.releaseAll();
  delay(3000);
  Keyboard.press(KEY_ESC); Keyboard.releaseAll(); delay(300);

  // ── Windows: Win+R → "cmd" → Enter → curl command ───────────────
  Keyboard.press(KEY_LEFT_GUI); Keyboard.press('r');
  delay(120); Keyboard.releaseAll(); delay(700);
  Keyboard.print("cmd");
  delay(200); Keyboard.press(KEY_RETURN); Keyboard.releaseAll();
  delay(1500);
  Keyboard.print(cmd);
  delay(100); Keyboard.press(KEY_RETURN); Keyboard.releaseAll();
  delay(3000);
  Keyboard.press(KEY_ESC); Keyboard.releaseAll(); delay(300);

  // ── Linux: Ctrl+Alt+T → curl command ────────────────────────────
  Keyboard.press(KEY_LEFT_CTRL); Keyboard.press(KEY_LEFT_ALT); Keyboard.press('t');
  delay(120); Keyboard.releaseAll();
  delay(2500);
  Keyboard.print(cmd);
  delay(100); Keyboard.press(KEY_RETURN); Keyboard.releaseAll();
}

// ── Setup ─────────────────────────────────────────────────────────────
void setup() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, 1);
  FastLED.setBrightness(80);
  pinMode(BTN_PIN, INPUT_PULLUP);

  xTaskCreatePinnedToCore(ledTask, "LED", 8192, NULL, 1, NULL, 1);
  setLED(255, 80, 0);

  Keyboard.begin();
  Serial.begin(115200);
  delay(2000);

  unsigned long wifiStart = millis();
  connectWiFi();
  bool fastConnect = (millis() - wifiStart < 25000);

  MDNS.begin("claudebuddy");
  server.on("/",      HTTP_GET,  handleRoot);
  server.on("/chat",  HTTP_POST, handleChat);
  server.on("/clear", HTTP_POST, handleClear);
  server.on("/run",   HTTP_GET,  handleRun);
  server.begin();
  MDNS.addService("http", "tcp", 80);

  if (fastConnect) {
    autoLaunchCompanion();
  }

  printBanner(WiFi.localIP().toString());
  Serial.print(F("> "));
}

// ── Loop ──────────────────────────────────────────────────────────────
String inputBuf = "";

void loop() {
  server.handleClient();

  // Button: tap = clear, hold 3s = reset WiFi
  if (digitalRead(BTN_PIN) == LOW) {
    delay(30);
    if (digitalRead(BTN_PIN) == LOW) {
      unsigned long t0 = millis();
      setLED(255, 80, 0);
      while (digitalRead(BTN_PIN) == LOW) {
        if (millis() - t0 > 3000) {
          setLED(255, 0, 0);
          Serial.println(F("\n[Resetting WiFi — restarting...]"));
          WiFiManager wm; wm.resetSettings();
          delay(1500); ESP.restart();
        }
        delay(10);
      }
      clearHistory();
      Serial.println(F("\n[Conversation cleared]\n"));
      setLED(0, 0, 200);
      Serial.print(F("> "));
    }
  }

  // Serial terminal
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      inputBuf.trim();
      if (inputBuf.length() == 0) continue;
      String prompt = inputBuf;
      inputBuf = "";
      Serial.println();

      ledThinking = true;
      String reply = askClaude(prompt);
      setLED(0, 200, 0); delay(400); setLED(0, 0, 200);

      Serial.print(F("Claude: "));
      Serial.println(reply);
      Serial.println();
      Serial.print(F("> "));

    } else if (c == 127 || c == 8) {
      if (inputBuf.length() > 0) {
        inputBuf.remove(inputBuf.length() - 1);
        Serial.print(F("\b \b"));
      }
    } else {
      inputBuf += c;
      Serial.print(c);
    }
  }
}
