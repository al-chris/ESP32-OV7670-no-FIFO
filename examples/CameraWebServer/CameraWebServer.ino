#include <OV7670.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include "BMP.h"

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
          client.println("HTTP/1.1 200 OK");
          client.println("Content-type:text/html");
          client.println("Connection: close");
          client.println();
          client.print(
            "<!DOCTYPE html><html><head><title>ESP32-OV7670 Camera</title>"
            "<meta http-equiv='refresh' content='0.1'></head>"
            "<body><h1 style='font-family: sans-serif;'>ESP32-OV7670 Stream</h1>"
            "<img src='/camera' width='320' height='240'>"
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

      if (currentLine.endsWith("GET /camera")) {
        camera->oneFrame();
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: image/bmp");
        client.println("Content-Length: " + String(BMP::headerSize + camera->xres * camera->yres * 2));
        client.println("Connection: close");
        client.println();
        client.write((const uint8_t*)bmpHeader, BMP::headerSize);
        client.write((const uint8_t*)camera->frame, camera->xres * camera->yres * 2);
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
