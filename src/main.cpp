// NexoDrive — ESP32-S3 (AP + Web + uploads con fecha y nombre + descarga)
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// ---- WiFi/AP ----
static const char* AP_SSID = "NexoDrive_AP";
static const char* AP_PASS = "nexodrive123";

// ---- HTTP ----
AsyncWebServer server(80);

// ---- FS/Meta ----
static const char* META_PATH = "/meta.json";       // guarda { "/u/archivo": { ts, uploader } }
static const size_t MAX_UPLOAD = 10 * 1024 * 1024; // 10 MB

// ---- Helpers ----
static String filenameOnly(const String& p) {
  int i = p.lastIndexOf('/');
  return i >= 0 ? p.substring(i + 1) : p;
}

bool readMeta(DynamicJsonDocument& doc) {
  if (!LittleFS.exists(META_PATH)) { doc.to<JsonObject>(); return true; }
  File f = LittleFS.open(META_PATH, "r");
  if (!f) { doc.to<JsonObject>(); return false; }
  auto e = deserializeJson(doc, f);
  f.close();
  if (e) { doc.to<JsonObject>(); return false; }
  return true;
}

bool writeMeta(const DynamicJsonDocument& doc) {
  File f = LittleFS.open(META_PATH, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

bool initFS() {
  // Monta la partición llamada "littlefs" (de tu CSV de particiones)
  if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
    Serial.println("[FS] Error montando LittleFS");
    return false;
  }
  LittleFS.mkdir("/u"); // carpeta de usuario
  if (!LittleFS.exists(META_PATH)) { File f = LittleFS.open(META_PATH, "w"); f.print("{}"); f.close(); }
  Serial.printf("[FS] LittleFS OK. Usado %u / %u bytes\n",
                (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  return true;
}

// ---- Handlers ----
void handleList(AsyncWebServerRequest* req) {
  File root = LittleFS.open("/u");
  if (!root || !root.isDirectory())
    return req->send(500, "application/json", R"({"ok":false,"err":"fs"})");

  DynamicJsonDocument meta(4096); readMeta(meta);
  DynamicJsonDocument out(4096);
  JsonArray arr = out.createNestedArray("files");

  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (f.isDirectory()) continue;
    String p = "/u/" + filenameOnly(String(f.name()));
    JsonObject o = arr.createNestedObject();
    o["name"] = p;
    o["size"] = (uint32_t)f.size();

    uint64_t ts = meta[p]["ts"] | 0ULL;   // <— usar 64 bits
    o["ts"] = ts;

    o["uploader"] = meta[p]["uploader"] | "";
  }
  String s; serializeJson(out, s);
  req->send(200, "application/json", s);
}


void handleDownload(AsyncWebServerRequest* req) {
  if (!req->hasParam("path"))
    return req->send(400, "application/json", R"({"ok":false,"err":"path"})");

  String path = req->getParam("path")->value();
  if (!path.startsWith("/")) path = "/" + path;
  if (!path.startsWith("/u/")) path = "/u/" + filenameOnly(path);  // <-- clave

  if (!LittleFS.exists(path))
    return req->send(404, "application/json", R"({"ok":false,"err":"nf"})");

  auto* res = req->beginResponse(LittleFS, path, "application/octet-stream", true);
  res->addHeader("Content-Disposition", "attachment; filename=\"" + filenameOnly(path) + "\"");
  req->send(res);
}


// ---- Arduino ----
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[NexoDrive] Boot");

  WiFi.mode(WIFI_AP);
  bool apOK = WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[WiFi] AP SSID:%s PASS:%s IP:%s\n", AP_SSID, AP_PASS, ip.toString().c_str());
  if (!apOK) Serial.println("[WiFi] Error creando AP");

  bool fsOK = initFS();

  // CORS útil en pruebas
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

  // Rutas
  server.on("/health", HTTP_GET, [](AsyncWebServerRequest* r){
    r->send(200, "application/json", R"({"ok":true,"role":"NexoDrive"})");
  });
  server.on("/api/list", HTTP_GET, handleList);
  server.on("/api/download", HTTP_GET, handleDownload);

  // Upload: guarda en /u y actualiza meta.json con ts y uploader
server.on(
  "/api/upload",
  HTTP_POST,
  [](AsyncWebServerRequest* r){            // FIN de la petición (ya parseó campos)
    String name = r->hasParam("name", true) ? r->getParam("name", true)->value() : "";
    String up   = r->hasParam("uploader", true) ? r->getParam("uploader", true)->value() : "";
    uint64_t ts = r->hasParam("ts", true) ? strtoull(r->getParam("ts", true)->value().c_str(), nullptr, 10) : 0;

    if (name.length()) {
      DynamicJsonDocument meta(4096); readMeta(meta);
      String key = "/u/" + filenameOnly(name);
      JsonObject e = meta[key].to<JsonObject>();
      e["uploader"] = up;
      e["ts"] = ts;
      writeMeta(meta);
    }
    r->send(200, "application/json", R"({"ok":true})");
  },
  [](AsyncWebServerRequest* r, String fn, size_t idx, uint8_t* data, size_t len, bool fin){
    if (idx==0 && r->contentLength() > MAX_UPLOAD) { r->client()->close(); return; }
    String save = "/u/" + filenameOnly(fn);       // guarda siempre en /u
    if (idx==0) r->_tempFile = LittleFS.open(save, "w");
    if (len && r->_tempFile) r->_tempFile.write(data, len);
    if (fin && r->_tempFile) r->_tempFile.close();
  }
);

  if (fsOK) server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.begin();
  Serial.println("[HTTP] Servidor iniciado en puerto 80");
}

void loop() {}
