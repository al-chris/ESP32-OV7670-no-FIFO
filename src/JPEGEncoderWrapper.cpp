#include "JPEGEncoderWrapper.h"
#include "Config.h"
#include "Log.h"
#include <Arduino.h>

#if OV7670_ENABLE_JPEG

// Detect possible encoder headers at compile time. Prefer the JPEGENC library
// (bitbank2/JPEGENC) if available, otherwise detect other common encoder headers.
#if defined(__has_include)
#  if __has_include(<JPEGENC.h>)
#    include <JPEGENC.h>
#    define HAVE_JPEGENC 1
#  elif __has_include("JPEGENC.h")
#    include "JPEGENC.h"
#    define HAVE_JPEGENC 1
#  elif __has_include(<JPEGEncoder.h>)
#    include <JPEGEncoder.h>
#    define HAVE_JPEG_ENCODER 1
#  elif __has_include("JPEGEncoder.h")
#    include "JPEGEncoder.h"
#    define HAVE_JPEG_ENCODER 1
#  endif
#endif

// If JPEGENC is available, use it. Otherwise fall back to other encoder detection.
#if defined(HAVE_JPEGENC)

bool JPEGEncoderWrapper::encode(const uint8_t* rgb565, int xres, int yres, int quality, uint8_t* outBuffer, size_t* outLen)
{
  if (!outBuffer || !outLen) return false;

  // Input buffer is expected to be YUV422 (YUYV) coming from the OV7670 when
  // the camera is configured for YUV output. Each pair of pixels is stored as
  // [Y0][U][Y1][V]. We'll convert on-the-fly to RGB565 when filling MCU buffers
  // for the encoder to avoid large intermediate allocations.

  // Prefer JPEGENC (bitbank2) if available.
#if defined(HAVE_JPEGENC)
  JPEGENC jpg;
  JPEGENCODE jpe;

  int rc = jpg.open((uint8_t*)outBuffer, (int)OV7670_MAX_JPEG_SIZE);
  if (rc != JPEGE_SUCCESS) {
    DEBUG_PRINTLN("JPEGEncoderWrapper: jpg.open failed");
    return false;
  }

  int q = JPEGE_Q_HIGH;
  if (quality <= 25) q = JPEGE_Q_LOW;
  else if (quality <= 50) q = JPEGE_Q_MED;
  else if (quality <= 75) q = JPEGE_Q_HIGH;
  else q = JPEGE_Q_BEST;

  rc = jpg.encodeBegin(&jpe, xres, yres, JPEGE_PIXEL_RGB565, JPEGE_SUBSAMPLE_444, q);
  if (rc != JPEGE_SUCCESS) {
    DEBUG_PRINTLN("JPEGEncoderWrapper: encodeBegin failed");
    jpg.close();
    return false;
  }

  int mcuX = jpe.cx;
  int mcuY = jpe.cy;
  size_t mcuPixels = (size_t)mcuX * (size_t)mcuY;
  uint8_t* mcuBuf = (uint8_t*)malloc(mcuPixels * 2);
  if (!mcuBuf) {
    DEBUG_PRINTLN("JPEGEncoderWrapper: failed to allocate MCU buffer");
    jpg.close();
    return false;
  }

  for (int my = 0; my < yres; my += mcuY) {
    for (int mx = 0; mx < xres; mx += mcuX) {
      size_t idx = 0;
      for (int yy = 0; yy < mcuY; ++yy) {
        int srcY = my + yy;
        for (int xx = 0; xx < mcuX; ++xx) {
          int srcX = mx + xx;
          if (srcX < xres && srcY < yres) {
            // Convert YUYV -> RGB565 for pixel (srcX, srcY)
            const uint8_t* yuvLine = &rgb565[(size_t)srcY * (size_t)xres * 2];
            int pairX = srcX & ~1; // base even pixel of the pair
            size_t base = (size_t)pairX * 2;
            uint8_t Y0 = yuvLine[base + 0];
            uint8_t U  = yuvLine[base + 1];
            uint8_t Y1 = yuvLine[base + 2];
            uint8_t V  = yuvLine[base + 3];
            uint8_t Y = (srcX & 1) ? Y1 : Y0;

            int c = (int)Y - 16;
            int d = (int)U - 128;
            int e = (int)V - 128;
            int r = (298 * c + 409 * e + 128) >> 8;
            int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
            int b = (298 * c + 516 * d + 128) >> 8;
            if (r < 0) r = 0; else if (r > 255) r = 255;
            if (g < 0) g = 0; else if (g > 255) g = 255;
            if (b < 0) b = 0; else if (b > 255) b = 255;
            uint16_t p = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            mcuBuf[idx++] = (uint8_t)(p & 0xFF);
            mcuBuf[idx++] = (uint8_t)((p >> 8) & 0xFF);
          } else {
            mcuBuf[idx++] = 0; mcuBuf[idx++] = 0;
          }
        }
      }
      rc = jpg.addMCU(&jpe, mcuBuf, (int)mcuPixels);
      if (rc != JPEGE_SUCCESS) {
        DEBUG_PRINTLN("JPEGEncoderWrapper: addMCU failed");
        free(mcuBuf);
        jpg.close();
        return false;
      }
    }
  }

  int outSize = jpg.close();
  if (outSize <= 0) {
    DEBUG_PRINTLN("JPEGEncoderWrapper: jpg.close returned 0");
    free(mcuBuf);
    return false;
  }
  *outLen = (size_t)outSize;
  free(mcuBuf);
  return true;
#elif defined(HAVE_JPEG_ENCODER)
  // If another encoder was detected via JPEGEncoder.h, user must adapt wrapper.
  DEBUG_PRINTLN("JPEGEncoderWrapper: non-JPEGENC encoder detected but wrapper needs wiring");
  return false;
#else
  DEBUG_PRINTLN("JPEGEncoderWrapper: encoder library not detected at compile time");
  return false;
#endif
}

bool JPEGEncoderWrapper::available()
{
  return true;
}

#else // HAVE_JPEG_ENCODER

bool JPEGEncoderWrapper::encode(const uint8_t* rgb565, int xres, int yres, int quality, uint8_t* outBuffer, size_t* outLen)
{
  (void)rgb565; (void)xres; (void)yres; (void)quality; (void)outBuffer; (void)outLen;
  DEBUG_PRINTLN("JPEGEncoderWrapper: encoder library not detected at compile time");
  return false;
}

bool JPEGEncoderWrapper::available()
{
  return false;
}

#endif // HAVE_JPEG_ENCODER

#else // OV7670_ENABLE_JPEG

bool JPEGEncoderWrapper::encode(const uint8_t* rgb565, int xres, int yres, int quality, uint8_t* outBuffer, size_t* outLen)
{
  (void)rgb565; (void)xres; (void)yres; (void)quality; (void)outBuffer; (void)outLen;
  DEBUG_PRINTLN("JPEGEncoderWrapper: JPEG support compiled out (OV7670_ENABLE_JPEG=0)");
  return false;
}

bool JPEGEncoderWrapper::available()
{
  return false;
}

#endif // OV7670_ENABLE_JPEG
