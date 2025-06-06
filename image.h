#include <cstdint>
#ifndef IMAGE_H

struct Image {
    uint64_t width, height, channels;
    uint32_t *data;
};

Image loadBMP(const char *filename);

#define IMAGE_H
#endif
