#include "image.h"

#include <cstdio>

Image loadBMP(const char *filename) {
    Image image = {0, 0, 0, 0};

    FILE *file = 0;

#if _WIN64
    fopen_s(&file, filename, "rb");
#else
    file = fopen(filename, "rb");
#endif

    if (!file) {
        fprintf(stderr, "Error: could not open file %s\n", filename);
    }

    unsigned char header[54];
    if (fread(header, 1, 54, file) != 54) {
        fprintf(stderr, "Error: could not read BMP header\n");
    }

    if (header[0] != 'B' || header[1] != 'M') {
        fprintf(stderr, "Error: invalid BMP file\n");
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

            image.data[i] = (A << 24) | (B << 16) | (G << 8) | R;
        }
    }

    fclose(file);
    image.channels = 3;

    return image;
}
