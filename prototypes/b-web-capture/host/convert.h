// Umwandlung BGRA (8 Bit, Bytefolge B, G, R, A) nach UYVY 4:2:2 und UYVA (UYVY + Alpha-Ebene).
// Ohne Qt, damit sie für sich prüfbar ist.
//
// Layout laut NDI-SDK-Dokumentation (nicht im SDK-Header geprüft, das SDK ist nicht installiert):
//   UYVY: je zwei Pixel 4 Byte U0 Y0 V0 Y1, Zeilenabstand = Breite * 2.
//   UYVA: UYVY-Ebene, direkt danach eine Alpha-Ebene mit 1 Byte je Pixel.
//   https://docs.ndi.video/all/developing-with-ndi/sdk/frame-types
// Farbmatrix: BT.709, begrenzter Bereich (Y 16..235, U/V 16..240). Annahme, siehe Bericht.
#pragma once

#include <cstddef>
#include <cstdint>

// width muss gerade sein. bgraStride in Byte.
void bgraToUyvy(const uint8_t *bgra, size_t bgraStride, int width, int height, uint8_t *uyvy);

// Wie bgraToUyvy, zusätzlich Alpha-Ebene (width * height Byte) nach alpha.
void bgraToUyva(const uint8_t *bgra, size_t bgraStride, int width, int height, uint8_t *uyvy, uint8_t *alpha);
