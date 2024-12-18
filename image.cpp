#include "image.hpp"
#include <cstdio>
#include <iostream>
#include <stdexcept>

Image loadBMP(const char *filename) {
    Image image;
    FILE *file = fopen(filename, "rb");
    if (!file) {
        throw std::runtime_error("failed to open file!");
    }

    unsigned char header[54];
    if (fread(header, 1, 54, file) != 54) {
        throw std::runtime_error("Not a correct BMP file");
    }

    if (header[0] != 'B' || header[1] != 'M') {
        throw std::runtime_error("Not a correct BMP file");
    }

    uint32_t dataPos = *(int *)&(header[0x0A]);
    uint32_t imageSize = *(int *)&(header[0x22]);
    image.width = *(int *)&(header[0x12]);
    image.height = *(int *)&(header[0x16]);

    if (imageSize == 0) {
        imageSize = image.width * image.height * 3;
    }

    if (dataPos == 0) {
        dataPos = 54;
    }

    image.data = new unsigned char[imageSize];
    fread(image.data, 1, imageSize, file);
    fclose(file);

    std::cout << "Loaded image: " << filename << '\n';

    return image;
}
