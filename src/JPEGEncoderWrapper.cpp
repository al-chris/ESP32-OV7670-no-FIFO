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

  size_t pixelCount = (size_t)xres * (size_t)yres;
  size_t rgb888Size = pixelCount * 3;
  uint8_t* rgb888 = (uint8_t*)malloc(rgb888Size);
  if (!rgb888) {
    DEBUG_PRINTLN("JPEGEncoderWrapper: failed to allocate RGB888 buffer");
    return false;
  }

  for (size_t i = 0; i < pixelCount; ++i) {
    uint16_t p = ((const uint16_t*)rgb565)[i];
    uint8_t r = (uint8_t)(((p >> 11) & 0x1F) * 255 / 31);
    uint8_t g = (uint8_t)(((p >> 5) & 0x3F) * 255 / 63);
    uint8_t b = (uint8_t)(((p) & 0x1F) * 255 / 31);
    rgb888[i*3 + 0] = r;
    rgb888[i*3 + 1] = g;
    rgb888[i*3 + 2] = b;
  }

  // Prefer JPEGENC (bitbank2) if available.
#if defined(HAVE_JPEGENC)
  JPEGENC jpg;
  JPEGENCODE jpe;

  int rc = jpg.open((uint8_t*)outBuffer, (int)OV7670_MAX_JPEG_SIZE);
  if (rc != JPEGE_SUCCESS) {
    DEBUG_PRINTLN("JPEGEncoderWrapper: jpg.open failed");
    free(rgb888);
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
    free(rgb888);
    return false;
  }

  int mcuX = jpe.cx;
  int mcuY = jpe.cy;
  size_t mcuPixels = (size_t)mcuX * (size_t)mcuY;
  uint8_t* mcuBuf = (uint8_t*)malloc(mcuPixels * 2);
  if (!mcuBuf) {
    DEBUG_PRINTLN("JPEGEncoderWrapper: failed to allocate MCU buffer");
    jpg.close();
    free(rgb888);
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
            uint16_t p = ((const uint16_t*)rgb565)[srcY * xres + srcX];
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
        free(rgb888);
        return false;
      }
    }
  }

  int outSize = jpg.close();
  if (outSize <= 0) {
    DEBUG_PRINTLN("JPEGEncoderWrapper: jpg.close returned 0");
    free(mcuBuf);
    free(rgb888);
    return false;
  }
  *outLen = (size_t)outSize;
  free(mcuBuf);
  free(rgb888);
  return true;
#elif defined(HAVE_JPEG_ENCODER)
  // If another encoder was detected via JPEGEncoder.h, user must adapt wrapper.
  DEBUG_PRINTLN("JPEGEncoderWrapper: non-JPEGENC encoder detected but wrapper needs wiring");
  free(rgb888);
  return false;
#else
  DEBUG_PRINTLN("JPEGEncoderWrapper: encoder library not detected at compile time");
  free(rgb888);
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
