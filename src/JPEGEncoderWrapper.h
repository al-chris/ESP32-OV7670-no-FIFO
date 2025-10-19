#pragma once
#include <Arduino.h>

// Lightweight wrapper to optionally use an external JPEG encoder.
// If a compatible encoder header is available at compile time (e.g. JPEGEncoder.h),
// it will be used. Otherwise the wrapper will report that JPEG encoding is unavailable.

class JPEGEncoderWrapper {
public:
  // Encode an RGB565 buffer into JPEG. Returns true on success and fills outBuffer/outLen.
  // - rgb565: pointer to raw frame buffer (xres * yres * 2 bytes)
  // - xres, yres: dimensions
  // - quality: 0-100 (best)
  // - outBuffer: pointer to caller-allocated buffer where encoded JPEG will be placed
  // - outLen: resulting length in bytes (output)
  static bool encode(const uint8_t* rgb565, int xres, int yres, int quality, uint8_t* outBuffer, size_t* outLen);

  // Returns true if a JPEG encoder was found at compile time and encoding is available.
  static bool available();
};
