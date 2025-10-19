#pragma once

#include "rom/lldesc.h"
#include <stdlib.h>
#include "esp_heap_caps.h"

class DMABuffer {
  public:
    lldesc_t descriptor;
    unsigned char *buffer;
    int len;
    DMABuffer(int bytes)
    {
      len = bytes;
  buffer = (unsigned char*)heap_caps_malloc(bytes, MALLOC_CAP_DMA);
      descriptor.length = bytes;
      descriptor.size = bytes;
      descriptor.owner = 1;
      descriptor.sosf = 0;
      descriptor.buf = (unsigned char*)buffer;
      descriptor.offset = 0;
      descriptor.empty = 0;
      descriptor.eof = 1;
      descriptor.qe.stqe_next = nullptr;
    }
    ~DMABuffer() {
  if(buffer) heap_caps_free(buffer);
    }
    void next(DMABuffer *n)
    {
      descriptor.qe.stqe_next = &(n->descriptor);
    }
    int sampleCount()
    {
      return len / 4; // dword count
    }
};
