#include "web.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "layouts.h"
#include "version.h"
#include "web_page.generated.h"

namespace web {
namespace {

constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;
constexpr int MAX_BAD_CODES           = 5;
constexpr uint32_t LOCKOUT_MS         = 60000;
constexpr const char* CODE_HEADER     = "X-Code";

WebServer* g_server = nullptr;
Hooks g_hooks;
State g_state = State::Off;
String g_ssid;
String g_error;
String g_code;  // access code shown on the pad's screen
uint32_t g_connectStarted = 0;
bool g_mdns               = false;

int g_badCodes       = 0;
uint32_t g_lockUntil = 0;

bool g_otaOk = false;
String g_otaError;
uint32_t g_rebootAt = 0;

void sendJson(int code, bool ok, const char* field, const String& value)
{
    JsonDocument doc;
    doc["ok"] = ok;
    if (field) doc[field] = value;
    String body;
    serializeJson(doc, body);
    g_server->send(code, "application/json; charset=utf-8", body);
}

bool lockedOut() { return g_lockUntil && (int32_t)(millis() - g_lockUntil) < 0; }

// Checks the access code. Every request that reads the macros, changes them,
// types on the computer or installs firmware needs it, so someone else on
// the same network cannot do any of that.
bool codeOk()
{
    if (lockedOut()) return false;
    const String given = g_server->header(CODE_HEADER);
    if (g_code.length() && given == g_code) {
        g_badCodes = 0;
        return true;
    }
    // A request without any code (page opened for the first time) is not a guess.
    if (given.isEmpty()) return false;
    if (++g_badCodes >= MAX_BAD_CODES) {
        g_badCodes  = 0;
        g_lockUntil = millis() + LOCKOUT_MS;
    }
    return false;
}

void denied()
{
    if (lockedOut()) sendJson(429, false, "error", "Too many wrong codes. Wait a minute and try again.");
    else sendJson(401, false, "error", "code");
}

void handleIndex()
{
    g_server->sendHeader("Cache-Control", "no-store");
    g_server->sendHeader("Content-Encoding", "gzip");
    g_server->send_P(200, "text/html; charset=utf-8", kWebPageGz, kWebPageGzLen);
}

void handleAuth()
{
    if (!codeOk()) return denied();
    sendJson(200, true, nullptr, "");
}

void handleGetConfig()
{
    if (!codeOk()) return denied();
    g_server->sendHeader("Cache-Control", "no-store");
    g_server->send(200, "application/json; charset=utf-8", g_hooks.getConfig());
}

void handleInfo()
{
    g_server->sendHeader("Cache-Control", "no-store");
    g_server->send(200, "application/json; charset=utf-8", g_hooks.getInfo());
}

void handleLayouts()
{
    String body = "[";
    for (size_t i = 0; i < layouts::kLayoutCount; ++i) {
        const layouts::LayoutDef* l = layouts::kLayouts[i];
        if (i) body += ",";
        body += String("{\"id\":\"") + l->id + "\",\"name\":\"" + l->name + "\",\"mac\":" + (l->mac ? "true" : "false") + "}";
    }
    body += "]";
    g_server->sendHeader("Cache-Control", "max-age=3600");
    g_server->send(200, "application/json; charset=utf-8", body);
}

void handlePostConfig()
{
    if (!codeOk()) return denied();
    String msg;
    if (g_hooks.saveConfig(g_server->arg("plain"), g_server->arg("rev"), msg)) sendJson(200, true, "warnings", msg);
    else sendJson(400, false, "error", msg);
}

void handleRun()
{
    if (!codeOk()) return denied();
    String msg;
    if (g_hooks.runMacro(g_server->arg("plain"), msg)) sendJson(200, true, nullptr, "");
    else sendJson(400, false, "error", msg);
}

// Firmware upload (multipart, field "firmware"), written straight to the
// spare OTA partition; the pad restarts into it when it is complete.
void handleUpdateUpload()
{
    HTTPUpload& up = g_server->upload();
    switch (up.status) {
        case UPLOAD_FILE_START:
            g_otaError = "";
            g_otaOk    = codeOk();
            if (!g_otaOk) return;
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) g_otaError = Update.errorString();
            break;
        case UPLOAD_FILE_WRITE:
            if (g_otaOk && g_otaError.isEmpty() && Update.write(up.buf, up.currentSize) != up.currentSize) {
                g_otaError = Update.errorString();
            }
            break;
        case UPLOAD_FILE_END:
            if (g_otaOk && g_otaError.isEmpty() && !Update.end(true)) g_otaError = Update.errorString();
            break;
        case UPLOAD_FILE_ABORTED:
            if (g_otaOk) Update.abort();
            g_otaError = "upload aborted";
            break;
        default: break;
    }
}

void handleUpdateDone()
{
    if (!g_otaOk) return denied();
    if (g_otaError.length()) {
        if (Update.isRunning()) Update.abort();
        return sendJson(500, false, "error", g_otaError);
    }
    sendJson(200, true, "version", MACROCARD_VERSION);
    g_rebootAt = millis() + 1000;  // let the reply go out first
}

void startServer()
{
    g_server = new WebServer(80);
    const char* headers[] = {CODE_HEADER};
    g_server->collectHeaders(headers, 1);
    g_server->on("/", HTTP_GET, handleIndex);
    g_server->on("/api/auth", HTTP_GET, handleAuth);
    g_server->on("/api/config", HTTP_GET, handleGetConfig);
    g_server->on("/api/config", HTTP_POST, handlePostConfig);
    g_server->on("/api/info", HTTP_GET, handleInfo);
    g_server->on("/api/layouts", HTTP_GET, handleLayouts);
    g_server->on("/api/run", HTTP_POST, handleRun);
    g_server->on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
    g_server->onNotFound([] { g_server->send(404, "text/plain", "not found"); });
    g_server->begin();
}

}  // namespace

void setHooks(const Hooks& hooks) { g_hooks = hooks; }

void setCode(const String& code) { g_code = code; }

void start(const String& ssid, const String& password)
{
    stop();
    g_ssid  = ssid;
    g_error = "";
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), password.c_str());
    g_connectStarted = millis();
    g_state          = State::Connecting;
}

void stop()
{
    if (g_server) {
        g_server->stop();
        delete g_server;
        g_server = nullptr;
    }
    if (g_mdns) {
        MDNS.end();
        g_mdns = false;
    }
    if (g_state != State::Off) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
    g_state = State::Off;
}

void loop()
{
    if (g_rebootAt && (int32_t)(millis() - g_rebootAt) >= 0) ESP.restart();
    switch (g_state) {
        case State::Connecting:
            if (WiFi.status() == WL_CONNECTED) {
                if (!g_server) startServer();
                g_mdns = MDNS.begin(HOSTNAME);
                if (g_mdns) MDNS.addService("http", "tcp", 80);
                g_state = State::Online;
            } else if (millis() - g_connectStarted > CONNECT_TIMEOUT_MS) {
                wl_status_t st = WiFi.status();
                g_error = st == WL_NO_SSID_AVAIL    ? "network not found"
                          : st == WL_CONNECT_FAILED ? "wrong password?"
                                                    : "could not connect";
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
                g_state = State::Failed;
            }
            break;
        case State::Online:
            if (g_server) g_server->handleClient();
            // Router restarts etc.: the core reconnects by itself; the server
            // keeps running and answers again once the link is back.
            break;
        default: break;
    }
}

State state() { return g_state; }

const char* stateName()
{
    switch (g_state) {
        case State::Connecting: return "connecting";
        case State::Online: return "online";
        case State::Failed: return "failed";
        default: return "off";
    }
}

String ip() { return g_state == State::Online ? WiFi.localIP().toString() : String(); }
String lastError() { return g_error; }
String ssid() { return g_ssid; }

}  // namespace web
