#include <OV7670.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include "BMP.h"

// Edit these for your WiFi network
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASS";

// Pins (default mapping)
const int SIOD = 21, SIOC = 22, VSYNC = 34, HREF = 35, XCLK = 32, PCLK = 33;
const int D0 = 27, D1 = 19, D2 = 18, D3 = 15, D4 = 14, D5 = 13, D6 = 12, D7 = 4;

OV7670 *camera;
WiFiServer server(80);
unsigned char bmpHeader[BMP::headerSize];

void setup() {
  Serial.begin(115200);
  camera = new OV7670(OV7670::Mode::QQVGA_YUV422, SIOD, SIOC, VSYNC, HREF, XCLK, PCLK, D0, D1, D2, D3, D4, D5, D6, D7);
  BMP::construct16BitHeader(bmpHeader, camera->xres, camera->yres);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(250); Serial.print('.'); }
  Serial.println();
  Serial.print("IP: "); Serial.println(WiFi.localIP());
  server.begin();
}

void loop() {
  WiFiClient client = server.available();
  if (!client) return;
  while (client.connected()) {
    if (client.available()) {
      String req = client.readStringUntil('\n');
      if (req.indexOf("GET /capture") >= 0) {
        camera->oneFrame();
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: image/bmp");
        client.println("Content-Length: " + String(BMP::headerSize + camera->xres * camera->yres * 2));
        client.println("Connection: close");
        client.println();
        client.write((const uint8_t*)bmpHeader, BMP::headerSize);
        client.write((const uint8_t*)camera->frame, camera->xres * camera->yres * 2);
        break;
      }
    }
  }
  client.stop();
}
