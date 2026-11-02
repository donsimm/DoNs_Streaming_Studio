// Umwandlung BGRA nach UYVY/UYVA, siehe convert.h.
// Koeffizienten BT.709 (Kr = 0,2126, Kb = 0,0722), auf 8 Bit begrenzten Bereich skaliert, mal 256:
//   Y  =  16 + ( 47 R + 157 G +  16 B) / 256
//   Cb = 128 + (-26 R -  86 G + 112 B) / 256
//   Cr = 128 + (112 R - 102 G -  10 B) / 256
// Quelle der Kennwerte: ITU-R BT.709-6, Tabelle 3, https://www.itu.int/rec/R-REC-BT.709
// Chroma: Mittel aus zwei benachbarten Pixeln.
#include "convert.h"

static inline void pairToUyvy(const uint8_t *p, uint8_t *out)
{
    const int b0 = p[0], g0 = p[1], r0 = p[2];
    const int b1 = p[4], g1 = p[5], r1 = p[6];
    const int r = r0 + r1, g = g0 + g1, b = b0 + b1;
    out[0] = static_cast<uint8_t>(128 + ((-26 * r - 86 * g + 112 * b) >> 9));
    out[1] = static_cast<uint8_t>(16 + ((47 * r0 + 157 * g0 + 16 * b0) >> 8));
    out[2] = static_cast<uint8_t>(128 + ((112 * r - 102 * g - 10 * b) >> 9));
    out[3] = static_cast<uint8_t>(16 + ((47 * r1 + 157 * g1 + 16 * b1) >> 8));
}

void bgraToUyvy(const uint8_t *bgra, size_t bgraStride, int width, int height, uint8_t *uyvy)
{
    for (int y = 0; y < height; ++y) {
        const uint8_t *src = bgra + y * bgraStride;
        uint8_t *dst = uyvy + static_cast<size_t>(y) * width * 2;
        for (int x = 0; x < width; x += 2, src += 8, dst += 4)
            pairToUyvy(src, dst);
    }
}

void bgraToUyva(const uint8_t *bgra, size_t bgraStride, int width, int height, uint8_t *uyvy, uint8_t *alpha)
{
    for (int y = 0; y < height; ++y) {
        const uint8_t *src = bgra + y * bgraStride;
        uint8_t *dst = uyvy + static_cast<size_t>(y) * width * 2;
        uint8_t *a = alpha + static_cast<size_t>(y) * width;
        for (int x = 0; x < width; x += 2, src += 8, dst += 4, a += 2) {
            pairToUyvy(src, dst);
            a[0] = src[3];
            a[1] = src[7];
        }
    }
}
