#include "image.hpp"

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <stdexcept>

struct Image {
    uint64_t width, height, channels;
    uint32_t *data;
};

Image loadBMP(const char *filename) {
    Image image;
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: could not open file %s\n", filename);
        std::runtime_error("Failed!");
    }

    unsigned char header[54];
    if (fread(header, 1, 54, file) != 54) {
        fprintf(stderr, "Error: could not read BMP header\n");
        std::runtime_error("Failed!");
    }

    if (header[0] != 'B' || header[1] != 'M') {
        fprintf(stderr, "Error: invalid BMP file\n");
        std::runtime_error("Failed!");
    }

    int dataPos = *(int *)&(header[0x0A]);
    int imageSize = *(int *)&(header[0x22]);
    image.width = *(int *)&(header[0x12]);
    image.height = *(int *)&(header[0x16]);

    if (imageSize == 0) {
        imageSize = image.width * image.height;
    }

    if (dataPos == 0) {
        dataPos = 54;
    }

    image.data = new uint32_t[imageSize];

    fseek(file, dataPos, SEEK_SET);

    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            int i = x + y * image.width;
            uint8_t B = fgetc(file);
            uint8_t G = fgetc(file);
            uint8_t R = fgetc(file);
            uint8_t A = 255;
            // uint8_t A = fgetc(file);
            image.data[i] = R << 24 | G << 16 | B << 8 | A;
        }
    }

    fclose(file);
    image.channels = 3;

    return image;
}
