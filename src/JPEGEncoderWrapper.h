#pragma once
#include <Arduino.h>

// Lightweight wrapper to optionally use an external JPEG encoder.
// If a compatible encoder header is available at compile time (e.g. JPEGEncoder.h),
// it will be used. Otherwise the wrapper will report that JPEG encoding is unavailable.

class JPEGEncoderWrapper {
public:
  // Encode a raw camera buffer into JPEG. Returns true on success and fills outBuffer/outLen.
  // By default the library expects the OV7670 to be configured for YUV422 output
  // (YUYV layout). The function will convert YUV->RGB565 on-the-fly before
  // feeding data to the JPEG encoder. The input buffer pointer should point to
  // xres*yres*2 bytes (YUV422) as produced by the camera when in YUV mode.
  // - xres, yres: dimensions
  // - quality: 0-100 (best)
  // - outBuffer: pointer to caller-allocated buffer where encoded JPEG will be placed
  // - outLen: resulting length in bytes (output)
  static bool encode(const uint8_t* rgb565, int xres, int yres, int quality, uint8_t* outBuffer, size_t* outLen);

  // Returns true if a JPEG encoder was found at compile time and encoding is available.
  static bool available();
};
