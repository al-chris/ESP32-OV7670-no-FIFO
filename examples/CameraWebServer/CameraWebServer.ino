#include <OV7670.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include "BMP.h"
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

// Pre-allocated JPEG buffer to avoid repeated malloc/free in streaming.
static uint8_t jpegBuffer[OV7670_MAX_JPEG_SIZE];
static size_t jpegLen = 0;

// UI/MJPEG control state
static bool mjpegStreaming = false;

// Runtime JPEG quality (can be changed via web UI). Initialized from compile-time default.
static int jpegQuality = OV7670_JPEG_QUALITY;
// Current camera mode (runtime). Initialized in setup when camera is created.
static OV7670::Mode currentMode = OV7670::Mode::QQVGA_RGB565;

void handleClient() {
  WiFiClient client = server.available();
  if (!client) return;

  Serial.println("New Client.");
  String currentLine = "";
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      if (c == '\n') {
        if (currentLine.length() == 0) {
          // Serve the control UI at root
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
            "<button onclick=setFormat('bmp')>Set BMP</button> "
            "<button onclick=setFormat('jpeg')>Set JPEG</button> "
            "<button onclick=startStream()>Start MJPEG</button> "
            "<button onclick=stopStream()>Stop MJPEG</button>"
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
            "</script>"
            "</body></html>"
          );
          client.println();
          break;
        } else {
          currentLine = "";
        }
      } else if (c!= '\r') {
        currentLine += c;
      }

      if (currentLine.indexOf("GET /camera") != -1) {
        camera->oneFrame();
        // Serve according to selected format in I2SCamera::imageFormat
        if (I2SCamera::imageFormat == I2SCamera::FORMAT_BMP) {
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: image/bmp");
          client.println("Content-Length: " + String(BMP::headerSize + camera->xres * camera->yres * 2));
          client.println("Connection: close");
          client.println();
          client.write((const uint8_t*)bmpHeader, BMP::headerSize);
          client.write((const uint8_t*)camera->frame, camera->xres * camera->yres * 2);
        } else {
          // JPEG
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

  if (currentLine.indexOf("GET /mjpeg") != -1 || currentLine.indexOf("GET /stream") != -1) {
        // Start MJPEG stream (alias /stream)
        Serial.println("Starting MJPEG stream");
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: multipart/x-mixed-replace; boundary=frameboundary");
        client.println("Connection: keep-alive");
        client.println();
        mjpegStreaming = true;
        while (client.connected() && mjpegStreaming) {
          camera->oneFrame();
          jpegLen = 0;
          bool ok = I2SCamera::encodeFrameToJPEG(jpegBuffer, &jpegLen, jpegQuality);
          if (!ok || jpegLen == 0) {
            // Send BMP fallback (clients may not accept BMP in MJPEG stream)
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
          }
          // Small delay to yield and control framerate
          delay(50);
        }
        mjpegStreaming = false;
      }

      // Simple control endpoints for UI actions
      if (currentLine.indexOf("GET /setFormat?mode=bmp") != -1) {
        I2SCamera::imageFormat = I2SCamera::FORMAT_BMP;
      }
      if (currentLine.indexOf("GET /setFormat?mode=jpeg") != -1) {
        I2SCamera::imageFormat = I2SCamera::FORMAT_JPEG;
      }
      // Set runtime resolution: /setResolution?mode=N where N is 0..3 matching OV7670::Mode
      if (currentLine.indexOf("GET /setResolution") != -1) {
        // Send a minimal HTTP 200 OK response so the fetch() caller sees success
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/plain");
        client.println("Connection: close");
        client.println();
        client.println("OK");
        int idx = currentLine.indexOf("mode=");
        if (idx != -1) {
          String v = currentLine.substring(idx + 5);
          int amp = v.indexOf(' ');
          if (amp != -1) v = v.substring(0, amp);
          int m = v.toInt();
          if (m < 0) m = 0;
          if (m > 3) m = 3;
          currentMode = (OV7670::Mode)m;
          Serial.printf("Changing resolution to mode %d\n", m);
          // Recreate camera with new mode
          if (camera) {
            camera->deinit();
            delete camera;
            camera = nullptr;
          }
          camera = new OV7670(currentMode,
                              SIOD, SIOC, VSYNC, HREF, XCLK, PCLK,
                              D0, D1, D2, D3, D4, D5, D6, D7);
          BMP::construct16BitHeader(bmpHeader, camera->xres, camera->yres);
        }
      }
      // Set runtime JPEG quality: /setQuality?value=NN
      if (currentLine.indexOf("GET /setQuality") != -1) {
        int idx = currentLine.indexOf("value=");
        if (idx != -1) {
          String v = currentLine.substring(idx + 6);
          int amp = v.indexOf(' ');
          if (amp != -1) v = v.substring(0, amp);
          int q = v.toInt();
          if (q < 1) q = 1;
          if (q > 100) q = 100;
          jpegQuality = q;
          Serial.printf("JPEG quality set to %d\n", jpegQuality);
        }
      }
      if (currentLine.indexOf("GET /stopstream") != -1) {
        mjpegStreaming = false;
      }
    }
  }
  client.stop();
  Serial.println("Client Disconnected.");
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- ESP32 OV7670 (No-FIFO) Camera Test ---");

  camera = new OV7670(OV7670::Mode::QQVGA_RGB565,
                      SIOD, SIOC, VSYNC, HREF, XCLK, PCLK,
                      D0, D1, D2, D3, D4, D5, D6, D7);

  BMP::construct16BitHeader(bmpHeader, camera->xres, camera->yres);

  Serial.printf("Connecting to %s ", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status()!= WL_CONNECTED) {
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
}
