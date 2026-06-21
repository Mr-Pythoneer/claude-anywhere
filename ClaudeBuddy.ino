/*
  Claude Buddy v3.0 — AtomS3U
  ─────────────────────────────────────────────────────────────────
  PRIMARY:  open http://claudebuddy.local in any browser (same WiFi)
  FALLBACK: serial terminal at 115200 baud

  LED    orange = connecting   blue = ready   pulsing = thinking
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
const char* SYSTEM_PROMPT  = "You are Claude Buddy, running on an M5Stack AtomS3U hardware device. "
                              "You can help with coding, questions, explanations, and anything else. "
                              "When writing code, always write the complete implementation.";
const int   MAX_TOKENS     = 8192;
const int   MAX_HISTORY    = 16;

// ── Conversation history ──────────────────────────────────────────────
String histRoles[MAX_HISTORY];
String histContent[MAX_HISTORY];
int    histSize = 0;

// ── LED state (written by main core, animated by Core 0 task) ─────────
volatile bool    ledThinking = false;
volatile uint8_t ledR = 255, ledG = 80, ledB = 0;  // default orange

// ── Web UI (stored in flash, not RAM) ─────────────────────────────────
const char INDEX_HTML[] PROGMEM = R"html(
<!DOCTYPE html><html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Claude Buddy</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#0d1117;color:#e6edf3;height:100dvh;display:flex;flex-direction:column}
header{padding:14px 20px;background:#161b22;border-bottom:1px solid #30363d;display:flex;align-items:center;gap:10px}
.dot{width:10px;height:10px;border-radius:50%;background:#238636;animation:pulse 2s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
h1{font-size:1rem;font-weight:600;color:#8b949e}
h1 span{color:#58a6ff}
#chat{flex:1;overflow-y:auto;padding:20px;display:flex;flex-direction:column;gap:16px}
.bubble{max-width:82%;padding:12px 16px;border-radius:12px;line-height:1.6;font-size:.9rem;white-space:pre-wrap;word-break:break-word}
.user{background:#1f6feb;align-self:flex-end;border-radius:12px 12px 2px 12px}
.claude{background:#161b22;border:1px solid #30363d;align-self:flex-start;border-radius:12px 12px 12px 2px}
.thinking{color:#8b949e;font-style:italic}
.thinking::after{content:'...';animation:dots 1.2s steps(3,end) infinite}
@keyframes dots{0%{content:'.'}33%{content:'..'}66%{content:'...'}}
#form{display:flex;padding:16px;gap:10px;background:#161b22;border-top:1px solid #30363d}
#inp{flex:1;padding:10px 14px;border-radius:8px;border:1px solid #30363d;background:#0d1117;color:#e6edf3;font-size:.95rem;outline:none;resize:none;height:42px;max-height:160px;overflow-y:auto}
#inp:focus{border-color:#58a6ff}
#btn{padding:10px 18px;border-radius:8px;border:none;background:#238636;color:#fff;font-weight:600;cursor:pointer;white-space:nowrap}
#btn:disabled{opacity:.4;cursor:default}
#btn:hover:not(:disabled){background:#2ea043}
code{background:#0d1117;padding:2px 6px;border-radius:4px;font-family:monospace}
pre{background:#0d1117;border:1px solid #30363d;border-radius:8px;padding:12px;overflow-x:auto;margin:4px 0}
pre code{background:none;padding:0}
</style>
</head>
<body>
<header><div class="dot"></div><h1><span>Claude Buddy</span> &nbsp;·&nbsp; claude-sonnet-4-6</h1></header>
<div id="chat"><div class="bubble claude">Hi! I'm Claude Buddy. Ask me anything — I can write code, answer questions, explain concepts, and more.</div></div>
<form id="form">
  <textarea id="inp" placeholder="Type a message… (Enter to send, Shift+Enter for newline)" rows="1"></textarea>
  <button id="btn" type="submit">Send</button>
</form>
<script>
const chat=document.getElementById('chat');
const inp=document.getElementById('inp');
const btn=document.getElementById('btn');

function escHtml(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}
function fmt(s){
  // code blocks
  s=s.replace(/```[\w]*\n?([\s\S]*?)```/g,(_,c)=>'<pre><code>'+escHtml(c.trim())+'</code></pre>');
  // inline code
  s=s.replace(/`([^`]+)`/g,(_,c)=>'<code>'+escHtml(c)+'</code>');
  return s;
}
function addMsg(text,cls,raw){
  const d=document.createElement('div');
  d.className='bubble '+cls;
  if(raw){d.innerHTML=text}else{d.textContent=text}
  chat.appendChild(d);
  chat.scrollTop=chat.scrollHeight;
  return d;
}
// auto-resize textarea
inp.addEventListener('input',()=>{inp.style.height='42px';inp.style.height=inp.scrollHeight+'px'});
inp.addEventListener('keydown',e=>{
  if(e.key==='Enter'&&!e.shiftKey){e.preventDefault();document.getElementById('form').dispatchEvent(new Event('submit'))}
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
    thinking.innerHTML=fmt(d.reply||d.error||'(empty response)');
  }catch(err){
    thinking.className='bubble claude';
    thinking.textContent='Error: '+err.message;
  }
  btn.disabled=false;
  chat.scrollTop=chat.scrollHeight;
  inp.focus();
});
inp.focus();
</script>
</body></html>
)html";

// ── LED task — runs on Core 0, animates independently of main loop ────
void ledTask(void*) {
  uint8_t hue = 0;
  while (true) {
    if (ledThinking) {
      leds[0] = CHSV(hue, 255, 200);  // full rainbow cycle
      hue += 3;
      FastLED.show();
      vTaskDelay(25 / portTICK_PERIOD_MS);
    } else {
      leds[0] = CRGB(ledR, ledG, ledB);
      FastLED.show();
      vTaskDelay(50 / portTICK_PERIOD_MS);
    }
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

void clearHistory() {
  histSize = 0;
}

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
    http.end();
    histSize--;
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
    server.send(400, "application/json", F("{\"error\":\"empty message\"}"));
    return;
  }
  ledThinking = true;
  String reply = askClaude(msg);
  setLED(0, 200, 0);
  delay(300);
  setLED(0, 0, 200);

  JsonDocument doc;
  doc["reply"] = reply;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleClear() {
  clearHistory();
  server.send(200, "application/json", F("{\"ok\":true}"));
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
    Serial.println(F("[WiFi] No saved credentials — portal active."));
    Serial.println(F("[WiFi]   Connect phone/laptop to: Claude-Buddy-Setup"));
    Serial.println(F("[WiFi]   Then open:               http://192.168.4.1"));
    setLED(255, 80, 0);
  });

  if (!wm.autoConnect("Claude-Buddy-Setup")) {
    Serial.println(F("[WiFi] Failed. Restarting..."));
    setLED(255, 0, 0);
    delay(3000);
    ESP.restart();
  }

  Serial.print(F("[WiFi] Connected — IP: "));
  Serial.println(WiFi.localIP());
  setLED(0, 0, 200);
}

// ── Banner ────────────────────────────────────────────────────────────
void printBanner(const String& ip) {
  Serial.println();
  Serial.println(F("  ╔═══════════════════════════════════════╗"));
  Serial.println(F("  ║       CLAUDE BUDDY  v3.0              ║"));
  Serial.println(F("  ║    AtomS3U · claude-sonnet-4-6        ║"));
  Serial.println(F("  ╠═══════════════════════════════════════╣"));
  Serial.println(F("  ║  OPEN IN ANY BROWSER (same WiFi):     ║"));
  Serial.print  (F("  ║    http://claudebuddy.local           "));
  Serial.println('║');
  Serial.print  (F("  ║    http://"));
  Serial.print(ip);
  for (int i = ip.length(); i < 28; i++) Serial.print(' ');
  Serial.println('║');
  Serial.println(F("  ╠═══════════════════════════════════════╣"));
  Serial.println(F("  ║  LED COLORS                           ║"));
  Serial.println(F("  ║    Orange       → connecting / setup  ║"));
  Serial.println(F("  ║    Blue solid   → ready               ║"));
  Serial.println(F("  ║    Blue pulsing → thinking            ║"));
  Serial.println(F("  ║    Green flash  → reply done          ║"));
  Serial.println(F("  ║    Red          → error               ║"));
  Serial.println(F("  ╠═══════════════════════════════════════╣"));
  Serial.println(F("  ║  BUTTON                               ║"));
  Serial.println(F("  ║    Tap    → clear conversation        ║"));
  Serial.println(F("  ║    Hold 3s → reset WiFi               ║"));
  Serial.println(F("  ╠═══════════════════════════════════════╣"));
  Serial.println(F("  ║  No WiFi? Connect to:                 ║"));
  Serial.println(F("  ║    SSID → Claude-Buddy-Setup          ║"));
  Serial.println(F("  ║    URL  → http://192.168.4.1          ║"));
  Serial.println(F("  ╚═══════════════════════════════════════╝"));
  Serial.println();
  Serial.println(F("  Or type here and press Enter."));
  Serial.println();
}

// ── Auto-open web UI in browser on any OS via HID keyboard ───────────
// Tries Mac → Windows → Linux in sequence with ESC pauses between them.
// Each approach is harmless on other platforms (worst case: reloads a page).
void autoOpenBrowser() {
  delay(1500);

  // ── Mac: Cmd+Space (Spotlight) → URL → Enter ──────────────────────
  Keyboard.press(KEY_LEFT_GUI);
  Keyboard.press(' ');
  delay(120);
  Keyboard.releaseAll();
  delay(700);
  Keyboard.print("http://claudebuddy.local");
  delay(100);
  Keyboard.press(KEY_RETURN);
  Keyboard.releaseAll();
  delay(3500);
  Keyboard.press(KEY_ESC);   // close Spotlight if still open (no-op elsewhere)
  Keyboard.releaseAll();
  delay(300);

  // ── Windows: Win+R (Run dialog) → URL → Enter ─────────────────────
  Keyboard.press(KEY_LEFT_GUI);
  Keyboard.press('r');
  delay(120);
  Keyboard.releaseAll();
  delay(700);
  Keyboard.print("http://claudebuddy.local");
  delay(100);
  Keyboard.press(KEY_RETURN);
  Keyboard.releaseAll();
  delay(3500);
  Keyboard.press(KEY_ESC);   // close Run dialog if URL was already handled (no-op elsewhere)
  Keyboard.releaseAll();
  delay(300);

  // ── Linux: Ctrl+Alt+T (terminal) → xdg-open → Enter ──────────────
  Keyboard.press(KEY_LEFT_CTRL);
  Keyboard.press(KEY_LEFT_ALT);
  Keyboard.press('t');
  delay(120);
  Keyboard.releaseAll();
  delay(2500);
  Keyboard.print("xdg-open http://claudebuddy.local");
  delay(100);
  Keyboard.press(KEY_RETURN);
  Keyboard.releaseAll();
}

// ── Setup ─────────────────────────────────────────────────────────────
void setup() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, 1);
  FastLED.setBrightness(80);
  pinMode(BTN_PIN, INPUT_PULLUP);

  // Start LED animation task on Core 0 (main loop runs on Core 1)
  xTaskCreatePinnedToCore(ledTask, "LED", 2048, NULL, 2, NULL, 0);

  setLED(255, 80, 0);  // orange = booting

  Keyboard.begin();      // register HID before USB starts
  Serial.begin(115200);  // register CDC + launch USB stack (both HID + CDC now live)
  delay(2000);           // let host enumerate both devices

  unsigned long wifiStart = millis();
  connectWiFi();
  bool fastConnect = (millis() - wifiStart < 25000);  // saved credentials = fast

  MDNS.begin("claudebuddy");

  server.on("/",      HTTP_GET,  handleRoot);
  server.on("/chat",  HTTP_POST, handleChat);
  server.on("/clear", HTTP_POST, handleClear);
  server.begin();

  MDNS.addService("http", "tcp", 80);

  // Fire HID to auto-open terminal — only if WiFi was fast (saved credentials)
  // Skips the sequence if the captive portal ran (user is busy setting up WiFi)
  if (fastConnect) {
    autoOpenBrowser();    // ~15s: tries Mac → Windows → Linux in sequence
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
          WiFiManager wm;
          wm.resetSettings();
          delay(1500);
          ESP.restart();
        }
        delay(10);
      }
      clearHistory();
      Serial.println(F("\n[Conversation cleared]\n"));
      setLED(0, 0, 200);
      Serial.print(F("> "));
    }
  }

  // Serial terminal input
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
      setLED(0, 200, 0);   // green flash when done
      delay(400);
      setLED(0, 0, 200);   // back to blue

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
