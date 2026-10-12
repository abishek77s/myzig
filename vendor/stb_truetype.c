// The TrueType/OpenType rasterizer MyZig draws glyphs with, behind a small
// C surface so Zig declares a handful of externs instead of translating the
// whole header (which would cost compile time on every build).
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#include <stdlib.h>

void *mz_font_init(const unsigned char *data, int index) {
    int off = stbtt_GetFontOffsetForIndex(data, index);
    if (off < 0) return 0;
    stbtt_fontinfo *fi = malloc(sizeof(stbtt_fontinfo));
    if (!fi) return 0;
    if (!stbtt_InitFont(fi, data, off)) { free(fi); return 0; }
    return fi;
}
float mz_scale_em(void *fi, float px) { return stbtt_ScaleForMappingEmToPixels(fi, px); }
void mz_vmetrics(void *fi, int *asc, int *desc, int *gap) { stbtt_GetFontVMetrics(fi, asc, desc, gap); }
int mz_glyph_index(void *fi, int cp) { return stbtt_FindGlyphIndex(fi, cp); }
void mz_hmetrics(void *fi, int g, int *adv, int *lsb) { stbtt_GetGlyphHMetrics(fi, g, adv, lsb); }
int mz_kern(void *fi, int g1, int g2) { return stbtt_GetGlyphKernAdvance(fi, g1, g2); }
void mz_glyph_box(void *fi, int g, float s, float shift_x, int *x0, int *y0, int *x1, int *y1) {
    stbtt_GetGlyphBitmapBoxSubpixel(fi, g, s, s, shift_x, 0, x0, y0, x1, y1);
}
void mz_glyph_render(void *fi, unsigned char *out, int w, int h, int stride, float s, float shift_x, int g) {
    stbtt_MakeGlyphBitmapSubpixel(fi, out, w, h, stride, s, s, shift_x, 0, g);
}
