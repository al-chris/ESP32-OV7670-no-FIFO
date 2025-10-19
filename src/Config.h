#pragma once

// Library-wide configuration defaults. Users can define these macros in their project
// build flags or edit them here if necessary.

// If defined to 0, JPEG encoding support will be disabled even if an encoder is present.
#ifndef OV7670_ENABLE_JPEG
#define OV7670_ENABLE_JPEG 1
#endif

// Maximum size (bytes) to allocate for encoded JPEG output.
// Increase if you get truncated JPEGs. Default is conservative for small resolutions.
#ifndef OV7670_MAX_JPEG_SIZE
#define OV7670_MAX_JPEG_SIZE (320 * 240)
#endif

// Default JPEG quality (0-100)
#ifndef OV7670_JPEG_QUALITY
#define OV7670_JPEG_QUALITY 80
#endif
