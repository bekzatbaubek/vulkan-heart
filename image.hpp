#ifndef IMAGE_H

struct Image {
    int width, height, channels;
    unsigned char *data;
};

Image loadBMP(const char *filename);

#define IMAGE_H
#endif
