#include <OV7670.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include "JPEGEncoderWrapper.h"
#include "Config.h"

const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASS";

const int SIOD = 21, SIOC = 22, VSYNC = 34, HREF = 35, XCLK = 32, PCLK = 33;
const int D0 = 27, D1 = 19, D2 = 18, D3 = 15, D4 = 14, D5 = 13, D6 = 12, D7 = 4;

OV7670 *camera;
WiFiServer server(80);
static uint8_t jpegBuffer[OV7670_MAX_JPEG_SIZE];
static size_t jpegLen = 0;

void setup(){
  Serial.begin(115200);
  camera = new OV7670(OV7670::Mode::QQVGA_YUV422, SIOD, SIOC, VSYNC, HREF, XCLK, PCLK, D0, D1, D2, D3, D4, D5, D6, D7);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(250); Serial.print('.'); }
  Serial.println();
  Serial.print("IP: "); Serial.println(WiFi.localIP());
  server.begin();
}

void loop(){
  WiFiClient client = server.available();
  if(!client) return;
  while(client.connected()){
    if(client.available()){
      String req = client.readStringUntil('\n');
      if (req.indexOf("GET /stream") >= 0) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: multipart/x-mixed-replace; boundary=frameboundary");
        client.println("Connection: keep-alive");
        client.println();
        while (client.connected()){
          camera->oneFrame();
          jpegLen = 0;
          bool ok = I2SCamera::encodeFrameToJPEG(jpegBuffer, &jpegLen, OV7670_JPEG_QUALITY);
          if (ok && jpegLen > 0){
            client.println("--frameboundary");
            client.println("Content-Type: image/jpeg");
            client.println("Content-Length: " + String(jpegLen));
            client.println();
            client.write(jpegBuffer, jpegLen);
          } else {
            // if JPEG unavailable, send BMP frame (clients may not accept)
            client.println("--frameboundary");
            client.println("Content-Type: image/bmp");
            client.println("Content-Length: " + String(BMP::headerSize + camera->xres * camera->yres * 2));
            client.println();
            client.write((const uint8_t*)camera->frame, camera->xres * camera->yres * 2);
          }
          delay(50);
        }
      }
    }
  }
  client.stop();
}
