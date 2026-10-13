// The image decoder MyZig loads PNG, JPEG, GIF (the first frame) and BMP
// with, behind a small C surface like stb_truetype's.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#include "stb_image.h"

// mz_image_decode decodes data to RGBA bytes, 4 a pixel; free them with
// mz_image_free.
unsigned char *mz_image_decode(const unsigned char *data, int len, int *w, int *h) {
    int n;
    return stbi_load_from_memory(data, len, w, h, &n, 4);
}
const char *mz_image_error(void) { return stbi_failure_reason(); }
void mz_image_free(unsigned char *p) { stbi_image_free(p); }
