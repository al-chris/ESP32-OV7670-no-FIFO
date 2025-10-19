#include <OV7670.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include "BMP.h"
#include <JPEGENC.h>
#include "JPEGEncoderWrapper.h"
#include "Config.h"

// --- Pinout Configuration (Definitive Mapping) ---
const int SIOD = 21;      // SCCB Data (SDA)
const int SIOC = 22;      // SCCB Clock (SCL)
const int VSYNC = 34;     // Vertical Sync (Input-Only Pin)
const int HREF = 35;      // Horizontal Reference (Input-Only Pin)
const int XCLK = 32;      // System Master Clock (Output)
const int PCLK = 33;      // Pixel Clock (Input)
const int D0 = 27;        // Data Bus Bit 0
const int D1 = 19;        // Data Bus Bit 1
const int D2 = 18;        // Data Bus Bit 2
const int D3 = 15;        // Data Bus Bit 3
const int D4 = 14;        // Data Bus Bit 4
const int D5 = 13;        // Data Bus Bit 5
const int D6 = 12;        // Data Bus Bit 6
const int D7 = 4;         // Data Bus Bit 7

// --- Wi-Fi Credentials (edit these) ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// --- Global Objects ---
OV7670 *camera;
WiFiServer server(80);
unsigned char bmpHeader[BMP::headerSize];

// --- MEMORY OPTIMIZATION ---
// The default OV7670_MAX_JPEG_SIZE is often too large for the ESP32's RAM when
// declaring multiple static buffers, leading to a DRAM overflow linker error.
// We define a smaller, more reasonable buffer size here. 25KB is plenty for
// resolutions up to QVGA. Increase this if you need to use VGA resolution.
const int JPEG_BUFFER_SIZE = 25 * 1024;

// Pre-allocated JPEG buffer to avoid repeated malloc/free in streaming.
static uint8_t jpegBuffer[JPEG_BUFFER_SIZE];
static size_t jpegLen = 0;
// Cache last JPEG
static uint8_t lastJpeg[JPEG_BUFFER_SIZE];
static size_t lastFileLen = 0;

// Runtime JPEG quality (can be changed via web UI). Initialized from compile-time default.
static int jpegQuality = OV7670_JPEG_QUALITY;

// UI/MJPEG control state
static bool mjpegStreaming = false;
// Current camera mode (runtime). Initialized in setup when camera is created.
static OV7670::Mode currentMode = OV7670::Mode::QQVGA_YUV422;
// Pending reconfiguration requested by HTTP handler. Handled in loop().
volatile bool pendingReinit = false;
volatile OV7670::Mode pendingMode = OV7670::Mode::QQVGA_YUV422;
// Debounce for resolution change
unsigned long lastResolutionMillis = 0;
const unsigned long resolutionDebounceMs = 500;

void handleClient() {
  WiFiClient client = server.available();
  if (!client) {
    return;
  }
  Serial.println("New Client.");

  // Wait for data, with a timeout
  unsigned long startTime = millis();
  while (!client.available() && millis() - startTime < 1000) {
    delay(1);
  }

  // Read the first line of the request
  String req = client.readStringUntil('\r');
  client.readStringUntil('\n'); // Consume the '\n'

  // Read and discard the rest of the headers
  while (client.available()) {
    if (client.read() == '\r' && client.read() == '\n' && client.read() == '\r' && client.read() == '\n') {
      break; // Blank line indicates end of headers
    }
  }

  // --- Route the request based on the first line ---
  if (req.indexOf("GET /camera") != -1) {
    if (pendingReinit) {
      if (lastFileLen > 0) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: image/jpeg");
        client.println("Content-Length: " + String(lastFileLen));
        client.println("Connection: close");
        client.println();
        client.write(lastJpeg, lastFileLen);
      } else {
        client.println("HTTP/1.1 503 Service Unavailable");
        client.println("Content-Type: text/plain");
        client.println("Connection: close");
        client.println();
        client.println("Reinitializing camera, try again shortly");
      }
    } else {
      camera->oneFrame();
      if (I2SCamera::imageFormat == I2SCamera::FORMAT_BMP) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: image/bmp");
        client.println("Content-Length: " + String(BMP::headerSize + camera->xres * camera->yres * 2));
        client.println("Connection: close");
        client.println();
        client.write((const uint8_t*)bmpHeader, BMP::headerSize);
        client.write((const uint8_t*)camera->frame, camera->xres * camera->yres * 2);
      } else {
        jpegLen = 0;
        bool ok = I2SCamera::encodeFrameToJPEG(jpegBuffer, &jpegLen, jpegQuality);
        if (ok && jpegLen > 0) {
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: image/jpeg");
          client.println("Content-Length: " + String(jpegLen));
          client.println("Connection: close");
          client.println();
          client.write(jpegBuffer, jpegLen);
        } else {
          client.println("HTTP/1.1 500 Internal Server Error");
          client.println("Connection: close");
          client.println();
          client.println("JPEG encoding not available");
        }
      }
    }
  } else if (req.indexOf("GET /stream") != -1 || req.indexOf("GET /mjpeg") != -1) {
    Serial.println("Starting MJPEG stream");
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: multipart/x-mixed-replace; boundary=frameboundary");
    client.println("Connection: keep-alive");
    client.println();
    mjpegStreaming = true;
    while (client.connected() && mjpegStreaming) {
      if (pendingReinit) {
        if (lastFileLen > 0) {
          client.println("--frameboundary");
          client.println("Content-Type: image/jpeg");
          client.println("Content-Length: " + String(lastFileLen));
          client.println();
          client.write(lastJpeg, lastFileLen);
        } else {
          client.println("--frameboundary");
          client.println("Content-Type: text/plain");
          client.println("Content-Length: 36");
          client.println();
          client.println("Reinitializing camera, stream paused");
        }
        delay(200);
        continue;
      }
      camera->oneFrame();
      jpegLen = 0;
      bool ok = I2SCamera::encodeFrameToJPEG(jpegBuffer, &jpegLen, jpegQuality);
      if (!ok || jpegLen == 0) {
        client.println("--frameboundary");
        client.println("Content-Type: image/bmp");
        client.println("Content-Length: " + String(BMP::headerSize + camera->xres * camera->yres * 2));
        client.println();
        client.write((const uint8_t*)bmpHeader, BMP::headerSize);
        client.write((const uint8_t*)camera->frame, camera->xres * camera->yres * 2);
      } else {
        client.println("--frameboundary");
        client.println("Content-Type: image/jpeg");
        client.println("Content-Length: " + String(jpegLen));
        client.println();
        client.write(jpegBuffer, jpegLen);
        if (jpegLen <= sizeof(lastJpeg)) {
          memcpy(lastJpeg, jpegBuffer, jpegLen);
          lastFileLen = jpegLen;
        }
      }
      delay(50);
    }
    mjpegStreaming = false;
  } else if (req.indexOf("GET /status") != -1) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    String fmt = (I2SCamera::imageFormat == I2SCamera::FORMAT_JPEG) ? "jpeg" : "bmp";
    int modeInt = (int)currentMode;
    int x = camera ? camera->xres : 0;
    int y = camera ? camera->yres : 0;
    client.print("{\"mode\":"); client.print(modeInt);
    client.print(",\"pending\":"); client.print(pendingReinit ? "true" : "false");
    client.print(",\"jpegQuality\":"); client.print(jpegQuality);
    client.print(",\"format\":\""); client.print(fmt); client.print('\"');
    client.print(",\"xres\":"); client.print(x);
    client.print(",\"yres\":"); client.print(y);
    client.println("}");
  } else if (req.indexOf("GET /setFormat?mode=bmp") != -1) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("OK");
    I2SCamera::imageFormat = I2SCamera::FORMAT_BMP;
  } else if (req.indexOf("GET /setFormat?mode=jpeg") != -1) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("OK");
    I2SCamera::imageFormat = I2SCamera::FORMAT_JPEG;
  } else if (req.indexOf("GET /setQuality") != -1) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("OK");
    int idx = req.indexOf("value=");
    if (idx != -1) {
      String v = req.substring(idx + 6);
      int q = v.toInt();
      if (q < 1) q = 1;
      if (q > 100) q = 100;
      jpegQuality = q;
      Serial.printf("JPEG quality set to %d\n", jpegQuality);
    }
  } else if (req.indexOf("GET /setResolution") != -1) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("OK");
    int idx = req.indexOf("mode=");
    if (idx != -1) {
      String v = req.substring(idx + 5);
      int m = v.toInt();
      if (m < 0) m = 0;
      if (m > 3) m = 3;
      unsigned long now = millis();
      if (now - lastResolutionMillis >= resolutionDebounceMs) {
        if (!pendingReinit || pendingMode != (OV7670::Mode)m) {
          pendingMode = (OV7670::Mode)m;
          pendingReinit = true;
          lastResolutionMillis = now;
          Serial.printf("Resolution change queued to mode %d\n", m);
        } else {
          Serial.printf("Resolution change to mode %d already pending\n", m);
        }
      } else {
        Serial.printf("Resolution change to mode %d debounced (too soon)\n", m);
      }
    }
  } else if (req.indexOf("GET /stopstream") != -1) {
    mjpegStreaming = false;
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("OK");
  } else { // Default to serving the main HTML page
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/html");
    client.println("Connection: close");
    client.println();
    client.print(
      "<!DOCTYPE html><html><head><title>ESP32-OV7670 Camera</title>"
      "<meta name=viewport content='width=device-width, initial-scale=1'>"
      "</head><body style='font-family:sans-serif; margin:16px;'>"
      "<h1>ESP32-OV7670 Camera</h1>"
      "<img id=preview src='/camera' width='320' height='240' style='display:block;border:1px solid #ccc;margin-bottom:8px;'>"
      "<div>Format: <span id=fmt>BMP</span></div>"
      "<div style='margin-top:8px;'>"
      "<button id='btnBmp' onclick=setFormat('bmp')>Set BMP</button> "
      "<button id='btnJpeg' onclick=setFormat('jpeg')>Set JPEG</button> "
      "<button id='btnStart' onclick=startStream()>Start MJPEG</button> "
      "<button id='btnStop' onclick=stopStream()>Stop MJPEG</button>"
      "</div>"
      "<div style='margin-top:12px;'>"
      "<label for='quality'>JPEG quality: <span id=qval>" + String(jpegQuality) + "</span></label><br>"
      "<input id='quality' type='range' min='1' max='100' value='" + String(jpegQuality) + "' oninput='onQualityChange(this.value)' onchange='applyQuality(this.value)'/>"
      "</div>"
      "<div style='margin-top:12px;'>"
      "<label for='resolution'>Resolution:</label> "
      "<select id='resolution' onchange='applyResolution(this.value)'>"
      "<option value='0'>QQQVGA (tiny)</option>"
      "<option value='1' selected>QQVGA</option>"
      "<option value='2'>QVGA</option>"
      "<option value='3'>VGA</option>"
      "</select>"
      "</div>"
      "<div id='toast' style='position:fixed; bottom:16px; left:50%; transform:translateX(-50%); background:#333; color:#fff; padding:8px 12px; border-radius:6px; display:none; opacity:0.95;'></div>"
      "<script>"
      "function onQualityChange(v){ document.getElementById('qval').innerText = v; }"
      "async function applyQuality(v){ await fetch('/setQuality?value='+v); }"
      "async function applyResolution(v){ let res = await fetch('/setResolution?mode='+v); if (res && res.ok) { showToast('Resolution updated'); document.getElementById('preview').src='/camera?'+Date.now(); } else { showToast('Resolution request failed'); } }"
      "function showToast(msg){ var t=document.getElementById('toast'); t.innerText=msg; t.style.display='block'; setTimeout(()=>t.style.display='none',2000); }"
      "async function setFormat(m){ await fetch('/setFormat?mode='+m); document.getElementById('preview').src='/camera?'+Date.now(); document.getElementById('fmt').innerText = m.toUpperCase(); }"
      "function startStream(){ window.open('/stream','_blank'); }"
      "async function stopStream(){ await fetch('/stopstream'); }"
      "async function pollStatus(){ try { let r = await fetch('/status'); if (!r.ok) throw 0; let j = await r.json(); document.getElementById('fmt').innerText = j.format.toUpperCase(); document.getElementById('qval').innerText = j.jpegQuality; document.getElementById('quality').value = j.jpegQuality; var disabled = j.pending ? true : false; document.getElementById('btnBmp').disabled = disabled; document.getElementById('btnJpeg').disabled = disabled; document.getElementById('btnStart').disabled = disabled; document.getElementById('btnStop').disabled = disabled; document.getElementById('resolution').disabled = disabled; } catch(e) { /* ignore */ } }"
      "setInterval(pollStatus, 500); pollStatus();"
      "</script>"
      "</body></html>"
    );
  }

  // For non-streaming requests, the connection is closed here.
  // The streaming handler will manage its own connection lifetime.
  if (req.indexOf("GET /stream") == -1 && req.indexOf("GET /mjpeg") == -1) {
    client.stop();
    Serial.println("Client Disconnected.");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- ESP32 OV7670 (No-FIFO) Camera Test ---");

  camera = new OV7670(OV7670::Mode::QQVGA_YUV422,
                      SIOD, SIOC, VSYNC, HREF, XCLK, PCLK,
                      D0, D1, D2, D3, D4, D5, D6, D7);

  BMP::construct16BitHeader(bmpHeader, camera->xres, camera->yres);

  Serial.printf("Connecting to %s ", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");

  server.begin();
  Serial.print("Web Server Started. Go to: http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  handleClient();

  // Apply any pending resolution change requested from the HTTP handler.
  if (pendingReinit) {
    noInterrupts();
    OV7670::Mode newMode = pendingMode;
    pendingReinit = false;
    interrupts();

    Serial.printf("Applying queued resolution change to mode %d\n", (int)newMode);
    if (camera) {
      camera->deinit();
      delete camera;
      camera = nullptr;
    }
    camera = new OV7670(newMode,
                        SIOD, SIOC, VSYNC, HREF, XCLK, PCLK,
                        D0, D1, D2, D3, D4, D5, D6, D7);
    BMP::construct16BitHeader(bmpHeader, camera->xres, camera->yres);
    currentMode = newMode;
  }
}

