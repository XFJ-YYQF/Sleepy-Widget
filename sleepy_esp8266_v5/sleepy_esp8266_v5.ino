/*
 * Sleepy ESP8266 Widget  v5
 * ─────────────────────────────────────────────────────────
 *  库
 *    U8g2  ·  ArduinoJson 6.x
 *
 *  烧录设置
 *    Flash Size: 4MB (FS:2MB ...)
 *
 *  更多信息
 *    https://github.com/XFJ-YYQF/Sleepy-Widget
 * ─────────────────────────────────────────────────────────
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <U8g2lib.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// ── 硬件引脚 ───────────────────────────────────────────────
#define PIN_SDA 4    // D2
#define PIN_SCL 5    // D1
#define PIN_BTN 14   // D5 主按钮
#define PIN_RST 12   // D6 重置按钮
#define PIN_CH2 13   // D7 通道 2（GPIO13 电平检测）

U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, PIN_SCL, PIN_SDA, U8X8_PIN_NONE);
ESP8266WebServer srv(80);

// ── 配置变量 ───────────────────────────────────────────────
String cfgSSID, cfgPass, cfgServer, cfgSecret;
int  cfgSwType   = 0;    // 0=NO  1=NC  2=GPIO13电平
int  cfgTrigType = 0;    // 0=单击 1=双击 2=长按
int  cfgClickMs  = 300;
int  cfgDblMs    = 400;
int  cfgLongMs   = 800;
int  cfgTimeout  = 60;   // 息屏超时（秒）
int  cfgAliveId  = -1;   // GPIO13 模式：活着状态 ID
int  cfgDeadId   = -1;   // GPIO13 模式：似了状态 ID

// ── 运行时状态 ─────────────────────────────────────────────
struct SEntry { int id; String name; };
SEntry sl[24];
int    slCnt = 0, curId = -1;
String curName = "--";
bool   hasErr = false, isAP = false;

int    setPend   = -1;
ulong  lastFetch = 0;
const ulong FETCH_MS       = 15000;  // 亮屏轮询间隔
const ulong FETCH_MS_SLEEP = 60000;  // 息屏轮询间隔
const ulong FETCH_MS_ERR   = 30000;  // 出错后重试间隔

// ── 息屏 ───────────────────────────────────────────────────
ulong lastActive = 0;
bool  screenOn   = true;

// ── GPIO13 去抖状态 ────────────────────────────────────────
bool  adcAlive = true, adcLastAlive = true;
int   adcStable = 0;

// ── 主按钮状态机 ───────────────────────────────────────────
enum BtnSt { B_IDLE, B_DOWN1, B_UP1, B_DOWN2, B_HOLD_DONE };
BtnSt btnSt = B_IDLE;
ulong btnT  = 0;
bool  btnRaw = HIGH;

// ── 重置引脚 ───────────────────────────────────────────────
bool  rstRaw = HIGH, rstDone = true;
ulong rstAt  = 0;

// ══════════════════════════════════════════════════════════
// 配置文件（LittleFS）
// ══════════════════════════════════════════════════════════
void cfgLoad() {
  File f = LittleFS.open(F("/c"), "r");
  if (!f) return;
  StaticJsonDocument<512> d;
  if (!deserializeJson(d, f)) {
    cfgSSID      = d["s"]   | "";
    cfgPass      = d["p"]   | "";
    cfgServer    = d["u"]   | "";
    cfgSecret    = d["k"]   | "";
    cfgSwType    = d["sw"]  | 0;
    cfgTrigType  = d["tr"]  | 0;
    cfgClickMs   = d["ct"]  | 300;
    cfgDblMs     = d["di"]  | 400;
    cfgLongMs    = d["lp"]  | 800;
    cfgTimeout   = d["to"]  | 60;
    cfgAliveId   = d["aid"] | -1;
    cfgDeadId    = d["did"] | -1;
    while (cfgServer.endsWith("/")) cfgServer.remove(cfgServer.length() - 1);
  }
  f.close();
}

void cfgSave() {
  File f = LittleFS.open(F("/c"), "w");
  if (!f) return;
  StaticJsonDocument<512> d;
  d["s"]   = cfgSSID;    d["p"]   = cfgPass;
  d["u"]   = cfgServer;  d["k"]   = cfgSecret;
  d["sw"]  = cfgSwType;  d["tr"]  = cfgTrigType;
  d["ct"]  = cfgClickMs; d["di"]  = cfgDblMs;
  d["lp"]  = cfgLongMs;  d["to"]  = cfgTimeout;  // lp 之前遗漏，已修复
  d["aid"] = cfgAliveId; d["did"] = cfgDeadId;
  serializeJson(d, f);
  f.close();
}

// ══════════════════════════════════════════════════════════
// OLED
// ══════════════════════════════════════════════════════════
void oledDraw() {
  u8g2.clearBuffer();
  if (isAP) {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(0, 12); u8g2.print(F("-- AP Mode --"));
    u8g2.setCursor(0, 28); u8g2.print(F("WiFi: Sleepy-ESP8266"));
    u8g2.setCursor(0, 42); u8g2.print(F("(no password)"));
    u8g2.setCursor(0, 56); u8g2.print(F("-> 192.168.4.1"));
  } else {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(0, 7);  u8g2.print(F("Sleepy Widget"));
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(0, 16); u8g2.print(WiFi.localIP().toString());
    if (hasErr) { u8g2.setCursor(122, 16); u8g2.print(F("!")); }
    u8g2.drawHLine(0, 18, 127);
    u8g2.setCursor(0, 27); u8g2.print(F("Status"));
    u8g2.setFont(u8g2_font_wqy16_t_gb2312b);
    u8g2.drawUTF8X2(0, 63, curName.c_str());
  }
  u8g2.sendBuffer();
}

void oledMsg(const char* l1, const char* l2 = nullptr) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.setCursor(0, 15); u8g2.print(l1);
  if (l2) { u8g2.setCursor(0, 30); u8g2.print(l2); }
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setCursor(0, 50); u8g2.print(F("Powered By MinecraftXFJ"));
  u8g2.setCursor(0, 60); u8g2.print(F("For SiiWay's Sleepy Project"));
  u8g2.sendBuffer();
}

// ══════════════════════════════════════════════════════════
// HTTP
// ══════════════════════════════════════════════════════════
bool httpGet(const String& path, String& body) {
  if (cfgServer.isEmpty()) return false;
  String url = cfgServer + path;
  HTTPClient http;
  http.setTimeout(4000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(false); 
  int code = -1;
  ESP.wdtFeed();
  if (url.startsWith(F("https"))) {
    WiFiClientSecure wcs; wcs.setInsecure();
    if (http.begin(wcs, url)) code = http.GET();
  } else {
    WiFiClient wc;
    if (http.begin(wc, url)) code = http.GET();
  }
  ESP.wdtFeed();
  body = (code == 200) ? http.getString() : "";
  http.end();
  return code == 200;
}

// ══════════════════════════════════════════════════════════
// Sleepy API
// ══════════════════════════════════════════════════════════
bool apiList() {
  String b;
  if (!httpGet(F("/api/status/list"), b)) return false;
  DynamicJsonDocument d(1536);
  if (deserializeJson(d, b) || !d["success"].as<bool>()) return false;
  slCnt = 0;
  for (JsonObject o : d["status_list"].as<JsonArray>()) {
    if (slCnt >= 24) break;
    sl[slCnt++] = { o["id"], o["name"].as<String>() };
  }
  return slCnt > 0;
}

bool apiQuery() {
  String b;
  if (!httpGet(F("/api/status/query"), b)) return false;
  DynamicJsonDocument d(768);
  if (deserializeJson(d, b) || !d["success"].as<bool>()) return false;
  curId   = d["status"]["id"];
  curName = d["status"]["name"].as<String>();
  return true;
}

bool apiSet(int id) {
  // 预分配路径字符串，避免多次 String 拼接产生碎片
  String path;
  path.reserve(40 + cfgSecret.length());
  path = F("/api/status/set?status=");
  path += id;
  path += F("&secret=");
  path += cfgSecret;
  String b;
  if (!httpGet(path, b)) return false;
  DynamicJsonDocument d(128);
  if (deserializeJson(d, b) || !d["success"].as<bool>()) return false;
  curId = id;
  for (int i = 0; i < slCnt; i++)
    if (sl[i].id == id) { curName = sl[i].name; break; }
  return true;
}

// ══════════════════════════════════════════════════════════
// 网页后台
// ══════════════════════════════════════════════════════════
static const char CSS[] PROGMEM =
  "*{box-sizing:border-box;margin:0;padding:0}"
  "body{font:14px/1.6 -apple-system,sans-serif;background:#f0f0f0;color:#222}"
  ".w{max-width:460px;margin:0 auto;padding:14px}"
  "h1{font-size:1em;font-weight:700;padding:10px 0 8px;"
      "border-bottom:1px solid #ddd;margin-bottom:10px;color:#333}"
  ".c{background:#fff;border-radius:8px;padding:14px;margin-bottom:10px;"
      "box-shadow:0 1px 4px rgba(0,0,0,.08)}"
  ".c h2{font-size:.72em;font-weight:700;letter-spacing:.07em;"
         "text-transform:uppercase;color:#aaa;margin-bottom:10px}"
  ".badge{display:inline-block;font-size:1.05em;font-weight:700;"
         "padding:3px 12px;background:#222;color:#fff;border-radius:4px}"
  "label{display:block;font-size:.78em;color:#999;margin:8px 0 2px}"
  "input,select{width:100%;padding:7px 8px;border:1px solid #ddd;"
               "border-radius:5px;font-size:.9em;background:#fafafa}"
  "input:focus,select:focus{outline:none;border-color:#666;background:#fff}"
  ".btn{padding:7px 16px;border:none;border-radius:5px;"
       "font-size:.88em;font-weight:600;cursor:pointer;background:#222;color:#fff}"
  ".btn:hover{background:#444}"
  ".row{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-top:8px}"
  ".err{font-size:.78em;color:#c00;margin-top:6px}"
  "a.danger{color:#c00;font-size:.82em}"
  "a.muted{color:#888;font-size:.82em}";

static const char FOOTER[] PROGMEM =
  "<footer style='margin-top:18px;padding:14px 0 8px;"
  "border-top:1px solid #ddd;text-align:center;"
  "font-size:.75em;line-height:2;color:#bbb'>"
  "Powered By <a href='https://www.minecraftxfj.top' target=_blank style='color:#999'>MinecraftXFJ</a><br>"
  "For SiiWay&#39;s <a href='https://github.com/sleepy-project' target=_blank style='color:#999'>Sleepy Project</a><br>"
  "Thank you for <a href='https://claude.ai' target=_blank style='color:#999'>Claude</a>"
  " &amp; <a href='https://www.deepseek.com' target=_blank style='color:#999'>DeepSeek</a>&#39;s help<br>"
  "<a href='https://wyf9.top' target=_blank style='color:#999'>wyf9</a> is a cute little catgirl !!!"
  "</footer></div></body></html>";

// 流式发送页头
void sendPageHeader(int code = 200) {
  srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
  srv.send(code, F("text/html"), "");
  srv.sendContent(F("<!DOCTYPE html><html><head>"
    "<meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Sleepy</title><style>"));
  String css = String(FPSTR(CSS));
  if (!css.isEmpty()) srv.sendContent(css);   // 保护
  srv.sendContent(F("</style></head><body><div class=w>"));
}

// 流式发送页脚
void sendPageFooter() {
  srv.sendContent(String(FPSTR(FOOTER)));
}

// 小页面快捷发送
void sendPage(const String& body, int code = 200) {
  sendPageHeader(code);
  if (!body.isEmpty()) srv.sendContent(body);   // 保护
  sendPageFooter();
}

// 发送整数
// 跳过分块传输中空字符串
inline void sc(const String& s) {
  if (!s.isEmpty()) srv.sendContent(s);
}
void sendInt(int v) {
  char buf[8];
  itoa(v, buf, 10);
  srv.sendContent(buf);
}

// 发送 <option> 元素
void sendOpt(int val, bool sel, const String& label) {
  srv.sendContent(F("<option value="));
  sendInt(val);
  if (sel) srv.sendContent(F(" selected"));
  srv.sendContent(F(">"));
  srv.sendContent(label);
  srv.sendContent(F("</option>"));
}

// 主页面
void webRoot() {
  sendPageHeader();

  // ── 当前状态 ──────────────────────────────────────────────
  srv.sendContent(F("<h1>Sleepy Widget</h1>"
    "<div class=c><h2>Current Status</h2>"
    "<div class=row><span class=badge>"));
  sc(curName);
  srv.sendContent(F("</span></div>"));
  if (hasErr)
    srv.sendContent(F("<p class=err>Last request failed &mdash; check server URL and secret.</p>"));
  srv.sendContent(F("<p style='font-size:.78em;color:#bbb;margin-top:8px'>IP: "));
  srv.sendContent(isAP ? F("192.168.4.1") : WiFi.localIP().toString());
  srv.sendContent(F("&ensp;|&ensp;"));
  sc(cfgServer);
  srv.sendContent(F("</p></div>"));

  // ── 设置状态（GPIO13 模式时隐藏）────────────────────────
  if (slCnt > 0 && cfgSwType != 2) {
    srv.sendContent(F("<div class=c><h2>Set Status</h2>"
      "<form method=POST action=/set><select name=id>"));
    for (int i = 0; i < slCnt; i++)
      sendOpt(sl[i].id, sl[i].id == curId, sl[i].name);
    srv.sendContent(F("</select><div class=row>"
      "<button class=btn>Apply</button>"
      "<a href=/refresh class=muted>Refresh</a>"
      "</div></form></div>"));
  }

  // ── 配置表单 ──────────────────────────────────────────────
  srv.sendContent(F("<div class=c><h2>Configuration</h2>"
    "<form method=POST action=/save>"
    "<label>WiFi SSID</label><input name=s value='"));
  sc(cfgSSID);
  srv.sendContent(F("'><label>WiFi Password</label>"
    "<input type=password name=p value='"));
  sc(cfgPass);
  srv.sendContent(F("'><label>Server URL (no trailing /)</label>"
    "<input name=u placeholder='https://example.com' value='"));
  sc(cfgServer);
  srv.sendContent(F("'><label>Secret (SLEEPY_SECRET)</label>"
    "<input name=k value='"));
  sc(cfgSecret);
  srv.sendContent(F("'>"));

  // ── 按钮 / 开关设置 ───────────────────────────────────────
  srv.sendContent(F("<hr style='margin:14px 0;border:none;border-top:1px solid #eee'>"
    "<h2 style='font-size:.72em;font-weight:700;letter-spacing:.07em;"
    "text-transform:uppercase;color:#aaa;margin-bottom:10px'>Button / Switch</h2>"
    "<label>Switch Type</label>"
    "<select name=sw id=_sw onchange='_upd()'>"));
  sendOpt(0, cfgSwType == 0, F("Normally Open (NO)"));
  sendOpt(1, cfgSwType == 1, F("Normally Closed (NC)"));
  sendOpt(2, cfgSwType == 2, F("GPIO13 Level Sense"));
  srv.sendContent(F("</select>"));

  // NO/NC 设置
  srv.sendContent(F("<div id=_sw_s>"
    "<label>Trigger Mode</label><select name=tr>"));
  sendOpt(0, cfgTrigType == 0, F("Single Click"));
  sendOpt(1, cfgTrigType == 1, F("Double Click"));
  sendOpt(2, cfgTrigType == 2, F("Long Press"));
  srv.sendContent(F("</select>"
    "<label>Click Duration (ms)</label>"
    "<input type=number name=ct min=50 max=2000 value="));
  sendInt(cfgClickMs);
  srv.sendContent(F("><label>Double Click Interval (ms)</label>"
    "<input type=number name=di min=100 max=2000 value="));
  sendInt(cfgDblMs);
  srv.sendContent(F("><label>Long Press Duration (ms)</label>"
    "<input type=number name=lp min=200 max=5000 value="));
  sendInt(cfgLongMs);
  srv.sendContent(F("><label>Screen Timeout (s)</label>"
    "<input type=number name=to min=1 max=3600 value="));
  sendInt(cfgTimeout);
  srv.sendContent(F("></div>"));

  // GPIO13 设置
  srv.sendContent(F("<div id=_adc_s>"
    "<label>Screen Timeout (s)</label>"
    "<input type=number name=to2 min=1 max=3600 value="));
  sendInt(cfgTimeout);
  srv.sendContent(F("><p style='font-size:.8em;color:#888;margin-top:4px'>"
    "GPIO13 high = Alive, low = Dead</p>"));

  // Alive 状态选择
  srv.sendContent(F("<label>Alive Status (high level)</label><select name=aid>"
    "<option value=-1"));
  if (cfgAliveId == -1) srv.sendContent(F(" selected"));
  srv.sendContent(F(">-- not set --</option>"));
  for (int i = 0; i < slCnt; i++)
    sendOpt(sl[i].id, sl[i].id == cfgAliveId, sl[i].name);
  srv.sendContent(F("</select>"));

  // Dead 状态选择
  srv.sendContent(F("<label>Dead Status (low level)</label><select name=did>"
    "<option value=-1"));
  if (cfgDeadId == -1) srv.sendContent(F(" selected"));
  srv.sendContent(F(">-- not set --</option>"));
  for (int i = 0; i < slCnt; i++)
    sendOpt(sl[i].id, sl[i].id == cfgDeadId, sl[i].name);
  srv.sendContent(F("</select></div>"));

  // 保存 / 重置按钮
  srv.sendContent(F("<div class=row style='margin-top:10px'>"
    "<button class=btn>Save &amp; Reboot</button>"
    "<a href=/reset class=danger onclick=\"return confirm('Clear WiFi config?')\">"
    "Reset WiFi</a></div></form></div>"));

  // 根据 Switch Type 切换显示区域
  srv.sendContent(F("<script>"
    "function _upd(){"
    "var v=document.getElementById('_sw').value;"
    "document.getElementById('_sw_s').style.display=v!='2'?'':'none';"
    "document.getElementById('_adc_s').style.display=v=='2'?'':'none';"
    "}_upd();"
    "</script>"));

  sendPageFooter();
}

void webSave() {
  if (srv.hasArg("s")) cfgSSID   = srv.arg("s");
  if (srv.hasArg("p")) cfgPass   = srv.arg("p");
  if (srv.hasArg("u")) {
    cfgServer = srv.arg("u");
    while (cfgServer.endsWith("/")) cfgServer.remove(cfgServer.length() - 1);
  }
  if (srv.hasArg("k"))  cfgSecret  = srv.arg("k");
  if (srv.hasArg("tr")) cfgTrigType = constrain(srv.arg("tr").toInt(), 0, 2);
  if (srv.hasArg("ct")) cfgClickMs  = constrain(srv.arg("ct").toInt(), 50, 2000);
  if (srv.hasArg("di")) cfgDblMs    = constrain(srv.arg("di").toInt(), 100, 2000);
  if (srv.hasArg("lp")) cfgLongMs   = constrain(srv.arg("lp").toInt(), 200, 5000);
  if (srv.hasArg("sw")) cfgSwType   = constrain(srv.arg("sw").toInt(), 0, 2);
  if (srv.hasArg("to") || srv.hasArg("to2"))
    cfgTimeout = constrain(
      srv.hasArg("to2") ? srv.arg("to2").toInt() : srv.arg("to").toInt(), 1, 3600);
  if (srv.hasArg("aid")) cfgAliveId = srv.arg("aid").toInt();
  if (srv.hasArg("did")) cfgDeadId  = srv.arg("did").toInt();
  cfgSave();
  sendPage(F("<h1>Saved</h1><div class=c><p>Rebooting in 3 seconds...</p></div>"
             "<script>setTimeout(()=>location.href='/',3100)</script>"));
  delay(3000);
  ESP.restart();
}

void webSet() {
  if (!srv.hasArg("id")) { srv.sendHeader(F("Location"), F("/")); srv.send(302); return; }
  setPend = srv.arg("id").toInt();
  sendPage(F("<h1>Setting...</h1><div class=c><p>Applying, please wait...</p></div>"
             "<script>setTimeout(()=>location.href='/',3000)</script>"));
}

void webRefresh() {
  lastFetch = millis() - FETCH_MS;
  srv.sendHeader(F("Location"), F("/"));
  srv.send(302);
}

void webReset() {
  cfgSSID = cfgPass = "";
  cfgSave();
  sendPage(F("<h1>Reset</h1><div class=c><p>WiFi cleared. Rebooting...</p></div>"));
  delay(2000);
  ESP.restart();
}

// ══════════════════════════════════════════════════════════
// WiFi 连接 / AP 模式
// ══════════════════════════════════════════════════════════
bool wifiConnect() {
  if (cfgSSID.isEmpty()) return false;
  WiFi.persistent(false);
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfgSSID.c_str(), cfgPass.c_str());
  oledMsg("Connecting...", cfgSSID.c_str());
  for (int i = 0; i < 40; i++) {
    if (WiFi.status() == WL_CONNECTED &&
        WiFi.localIP() != IPAddress(0, 0, 0, 0)) return true;
    delay(500);
  }
  return false;
}

void startAP() {
  isAP = true;
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(F("Sleepy-ESP8266"));
  oledDraw();
}

// ══════════════════════════════════════════════════════════
// 按钮处理
// ══════════════════════════════════════════════════════════
inline bool btnActive(bool raw) {
  return (cfgSwType == 1) ? (raw == HIGH) : (raw == LOW);
}

void wakeScreen() {
  lastActive = millis();
  if (!screenOn) {
    screenOn   = true;
    lastFetch  = 0;
    u8g2.setPowerSave(0);
  }
}

void fireTrigger() {
  wakeScreen();
  if (slCnt == 0) return;
  int next = 0;
  for (int i = 0; i < slCnt; i++)
    if (sl[i].id == curId) { next = (i + 1) % slCnt; break; }
  setPend = sl[next].id;
}

void btnHandle() {
  bool raw    = digitalRead(PIN_BTN);
  bool active = btnActive(raw);
  bool prevAc = btnActive(btnRaw);
  ulong now   = millis();
  btnRaw = raw;

  bool pressed  = (active  && !prevAc);
  bool released = (!active && prevAc);

  switch (btnSt) {
    case B_IDLE:
      if (pressed) { wakeScreen(); btnSt = B_DOWN1; btnT = now; }
      break;

    case B_DOWN1:
      if (cfgTrigType == 2 && now - btnT >= (ulong)cfgLongMs) {
        fireTrigger(); btnSt = B_HOLD_DONE; break;
      }
      if (released) {
        ulong dur = now - btnT;
        if (dur >= 20 && dur <= (ulong)cfgClickMs) {
          if (cfgTrigType == 0) { fireTrigger(); btnSt = B_IDLE; }
          else if (cfgTrigType == 1) { btnSt = B_UP1; btnT = now; }
          else btnSt = B_IDLE;
        } else btnSt = B_IDLE;
      }
      break;

    case B_UP1:
      if (now - btnT > (ulong)cfgDblMs) { btnSt = B_IDLE; break; }
      if (pressed) { btnSt = B_DOWN2; btnT = now; }
      break;

    case B_DOWN2:
      if (released) {
        ulong dur = now - btnT;
        if (dur >= 20 && dur <= (ulong)cfgClickMs) fireTrigger();
        btnSt = B_IDLE;
      }
      break;

    case B_HOLD_DONE:
      if (released) btnSt = B_IDLE;
      break;
  }
}

void rstPinHandle() {
  bool cur = digitalRead(PIN_RST);
  if (cur == LOW && rstRaw == HIGH) { rstAt = millis(); rstDone = false; }
  if (cur == HIGH) rstDone = true;
  if (cur == LOW && !rstDone && millis() - rstAt > 3000) {
    rstDone = true;
    cfgSSID = cfgPass = "";
    cfgSave();
    oledMsg("Reset WiFi...");
    delay(800); ESP.restart();
  }
  rstRaw = cur;
}

// ══════════════════════════════════════════════════════════
// 息屏
// ══════════════════════════════════════════════════════════
ulong fetchInterval() {
  if (!screenOn) return FETCH_MS_SLEEP;
  if (hasErr)    return FETCH_MS_ERR;
  return FETCH_MS;
}

void checkScreenTimeout() {
  if (!screenOn || isAP) return;
  if (hasErr || WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastActive >= (ulong)cfgTimeout * 1000UL) {
    screenOn = false;
    u8g2.setPowerSave(1);
  }
}

// ══════════════════════════════════════════════════════════
// GPIO13 电平检测
// ══════════════════════════════════════════════════════════
bool readCh2Majority() {
  int h = 0;
  for (int i = 0; i < 5; i++) //5次多数投票
    if (digitalRead(PIN_CH2) == HIGH) h++;
  return h >= 3;
}

void checkAdcStatus() {
  if (cfgAliveId < 0 && cfgDeadId < 0) return;
  bool alive = readCh2Majority();

  if (alive == adcLastAlive) {
    adcStable++;
    if (adcStable >= 3 && alive != adcAlive) {
      adcAlive  = alive;
      adcStable = 0;
      int id = alive ? cfgAliveId : cfgDeadId;
      if (id >= 0) { setPend = id; wakeScreen(); }
    }
  } else {
    adcLastAlive = alive;
    adcStable    = 0;
  }
}

// ══════════════════════════════════════════════════════════
// Setup & Loop
// ══════════════════════════════════════════════════════════
void setup() {
  // Serial.begin(115200);  // 调试串口，debug用的

  u8g2.begin();
  u8g2.enableUTF8Print();
  oledMsg("Starting...");

  pinMode(PIN_BTN, INPUT_PULLUP);
  delay(10);
  btnRaw = digitalRead(PIN_BTN);

  pinMode(PIN_RST, INPUT_PULLUP);
  delay(10);
  rstRaw  = digitalRead(PIN_RST);
  rstDone = (rstRaw == LOW);

  LittleFS.begin();
  cfgLoad();

  if (cfgSwType == 2) {
    pinMode(PIN_CH2, INPUT);
    adcAlive     = readCh2Majority();  // 初始化
    adcLastAlive = adcAlive;
  }

  wakeScreen();

  if (wifiConnect()) {
    isAP = false;
    lastFetch = millis() - FETCH_MS;   // 触发 loop 首轮立即 fetch
  } else {
    startAP();
  }

  srv.on(F("/"),        HTTP_GET,  webRoot);
  srv.on(F("/save"),    HTTP_POST, webSave);
  srv.on(F("/set"),     HTTP_POST, webSet);
  srv.on(F("/refresh"), HTTP_GET,  webRefresh);
  srv.on(F("/reset"),   HTTP_GET,  webReset);
  srv.onNotFound([]{ srv.sendHeader(F("Location"), F("/")); srv.send(302); });
  srv.begin();
  oledDraw();
}

void loop() {
  srv.handleClient();
  btnHandle();
  rstPinHandle();

  if (isAP) return;

  // GPIO13 状态检测（每 400ms 一次）
  static ulong lastAdcChk = 0;
  if (cfgSwType == 2 && millis() - lastAdcChk >= 400) {
    lastAdcChk = millis();
    checkAdcStatus();
  }

  checkScreenTimeout();

  // 处理待设置状态
  if (setPend >= 0) {
    int id = setPend; setPend = -1;
    wakeScreen();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(0, 14); u8g2.print(F("Setting..."));
    u8g2.setFont(u8g2_font_wqy12_t_gb2312b);
    u8g2.setCursor(0, 52);
    for (int i = 0; i < slCnt; i++)
      if (sl[i].id == id) { u8g2.print(sl[i].name); break; }
    u8g2.sendBuffer();
    hasErr = !apiSet(id);
    oledDraw();
  }

  // 定时刷新
  if (millis() - lastFetch >= fetchInterval()) {
    if (slCnt == 0) apiList();
    int  prevId  = curId;
    bool prevErr = hasErr;
    hasErr = !apiQuery();
    lastFetch = millis();
    if (curId != prevId || (prevErr && !hasErr)) wakeScreen();
    if (screenOn) oledDraw();
  }
}
