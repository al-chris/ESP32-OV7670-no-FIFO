# ESP32-OV7670-no-FIFO

Lightweight driver for the OV7670 camera module (no-FIFO variant) using the ESP32 I2S peripheral and DMA.

- Capture RGB565 frames via I2S (no FIFO)
- Produce BMP frames (built-in)
- Optional JPEG encoding using external encoder libraries (JPEGENC recommended)
- Example MJPEG streaming server and simple snapshot examples

Quick links
- Library sources: `src/` (core drivers and helpers)
- Examples: `examples/CameraWebServer`, `examples/SingleFrameBMP`, `examples/SingleFrameJPEG`, `examples/MJPEGStream`
- Configurable defaults: `src/Config.h`

Contents
- `src/` — library sources and headers (OV7670, I2SCamera, DMABuffer, BMP helper, JPEG wrapper, etc.)
- `examples/` — multiple example sketches (BMP, JPEG, MJPEG)
- `library.properties` — library manifest and Arduino IDE metadata
- README.md — this file

Quick usage (minimal)
```cpp
#include <OV7670.h>
#include "Config.h"            // optional if you want to change defaults
#include "JPEGEncoderWrapper.h" // optional if using JPEG

// construct camera (use your pin mapping)
OV7670* camera = new OV7670(OV7670::Mode::QQVGA_RGB565,
                            SIOD, SIOC, VSYNC, HREF, XCLK, PCLK,
                            D0, D1, D2, D3, D4, D5, D6, D7);

// capture one frame
camera->oneFrame(); // blocks until frame captured

// serve BMP by default: camera->frame points at xres*yres*2 bytes (RGB565)
client.write((const uint8_t*)camera->frame, camera->xres * camera->yres * 2);

// To produce JPEG (if encoder available):
I2SCamera::imageFormat = I2SCamera::FORMAT_JPEG;
static uint8_t jpegBuf[OV7670_MAX_JPEG_SIZE];
size_t jpegLen = 0;
if (I2SCamera::encodeFrameToJPEG(jpegBuf, &jpegLen, 80)) {
  client.write(jpegBuf, jpegLen);
}
```

Examples
- `CameraWebServer` — browser UI + image preview (`/camera`) and MJPEG (`/mjpeg`) endpoints. Also includes buttons to switch BMP/JPEG and start/stop stream.
- `SingleFrameBMP` — serve a single BMP snapshot at `/capture`.
- `SingleFrameJPEG` — capture and encode a single JPEG at `/capture.jpg` (requires encoder).
- `MJPEGStream` — simple MJPEG streaming example (`/stream`).

Enabling JPEG / JPEGENC
- JPEG support is optional. The wrapper attempts to auto-detect a JPEG encoder at compile time.
- Recommended encoder: bitbank2/JPEGENC. Install it via Arduino Library Manager or clone it into your `Arduino/libraries/` folder.
- The wrapper checks for `JPEGENC.h`. If `JPEGENC.h` is present it will use `JPEGENC` APIs (open/encodeBegin/addMCU/close).
- If you have a different encoder, edit JPEGEncoderWrapper.cpp to call that library's encode API (the project contains comments where to adapt).

Configurable macros (in `src/Config.h`)
- `OV7670_ENABLE_JPEG` (0/1) — compile-time toggle to include/exclude JPEG encoder code paths.
- `OV7670_MAX_JPEG_SIZE` — preallocated maximum JPEG buffer size (increase if images are truncated).
- `OV7670_JPEG_QUALITY` — default JPEG quality used by examples.

How the API maps (cheat sheet)
- `OV7670(...)` — constructor: creates and configures module (pass pin mapping and mode)
- `camera->oneFrame()` — capture a single frame (blocks)
- `camera->frame` — pointer to raw RGB565 buffer (size `xres*yres*2`)
- `camera->xres`, `camera->yres` — resolution configured
- `I2SCamera::imageFormat` — set to `I2SCamera::FORMAT_BMP` or `I2SCamera::FORMAT_JPEG`
- `I2SCamera::encodeFrameToJPEG(outBuf, &outLen, quality)` — encode last-captured frame into JPEG (returns bool)

Quick start (Arduino IDE)
1. Copy this library directory into `Arduino/libraries/` or install it via a zip.
2. (Optional) Install JPEGENC (bitbank2/JPEGENC) if you want JPEG/MJPEG functionality.
3. Open one of the sketches from `examples/` in Arduino IDE.
4. Edit Wi‑Fi credentials and verify pin mapping.
5. Select your ESP32 board and upload. Open Serial Monitor at 115200 to see IP.

Arduino CLI (example)
```powershell
# compile
arduino-cli compile --fqbn esp32:esp32:esp32dev C:\path\to\libraries\ESP32-OV7670-no-FIFO\examples\CameraWebServer
# upload (Windows example)
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32dev C:\path\to\libraries\ESP32-OV7670-no-FIFO\examples\CameraWebServer
```

PlatformIO (example)
- Add to `platformio.ini`:
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = bitbank2/JPEGENC@^1.1.0 ; (optional)
```
- Build and upload via `pio run -t upload`.

Wiring (recommended)
- See the detailed mapping in CameraWebServer.ino and the section below (kept intentionally conservative to avoid flash/strapping pins).
- Key notes:
  - Use 3.3V power (camera VCC).
  - SIOC -> GPIO22 and SIOD -> GPIO21 (with 4.7k pull-ups if needed).
  - Avoid flash pins (GPIO 6..11). VSYNC/HREF use input-only pins (34/35) in the examples.

Troubleshooting
- No SCCB device found: verify SIOC/SIOD wiring and pull-ups. Run an I2C scanner to confirm camera address (commonly 0x21).
- Scrambled/noisy image: shorten wires, add decoupling caps, verify XCLK stability.
- Wrong color or bands: check VSYNC/HREF wiring and resolution match.
- Memory errors on compile/run: QQVGA (160x120) is safe on most ESP32 boards. For QVGA/VGA you will need PSRAM (e.g., ESP32-WROVER).

Limitations
- Memory: ESP32 internal RAM is limited. QQVGA (160x120 RGB565 ≈ 38 KB) is recommended. Higher resolutions may require PSRAM.
- Realtime capture: relies on I2S+DMA. Do not try CPU bit-banging for pixel capture.
- JPEG availability: depends on external encoder library installed at compile time.

Examples endpoints (summary)
- `CameraWebServer` — `/` (control UI), `/camera` (image), `/mjpeg` (MJPEG stream)
- `SingleFrameBMP` — `/capture` returns BMP
- `SingleFrameJPEG` — `/capture.jpg` returns JPEG (requires encoder)
- `MJPEGStream` — `/stream` MJPEG

Runtime JPEG quality endpoint
--------------------------------
The `CameraWebServer` example exposes a small web UI with a JPEG quality slider. Adjusting the slider updates the JPEG quality used for subsequent encodes.

- Endpoint: `/setQuality?value=NN` where `NN` is an integer from 1..100.
- Behavior: the slider sends the value to the ESP32 which updates an in-memory `jpegQuality` variable. The change takes effect immediately for `/camera` snapshots and ongoing MJPEG streams.

You can also set the quality programmatically by issuing an HTTP GET to the endpoint, for example:

```text
http://<device-ip>/setQuality?value=85
```

This runtime control coexists with the compile-time macro `OV7670_JPEG_QUALITY` which sets the initial default if no runtime value has been selected.

Contributing
- PRs welcome. Please keep changes small and focused; add or update examples where applicable.
- If you add support for other encoders, add a short note and example to this README and update JPEGEncoderWrapper.cpp.

References
- bitbank2/JPEGENC — https://github.com/bitbank2/JPEGENC (recommended encoder)
- bitluni/ESP32CameraI2S — https://github.com/bitluni/ESP32CameraI2S (similar/related work)

License
- MIT (see `library.properties` and LICENSE file if present)

---