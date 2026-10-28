// A small PNG reader for team logos: decodes a PNG held in memory and shrinks it to fit a
// box, keeping its shape and transparency. Plain C++ (no Arduino), so it's tested on a PC too.
//
// Supported: greyscale (1/2/4/8-bit), grey+alpha, RGB, RGBA, and palette images (1/2/4/8-bit,
// with transparency). Not supported: 16-bit colour and interlaced PNGs - re-save those as 8-bit,
// non-interlaced (the default in most editors).
#pragma once
#include <stdint.h>
#include <stddef.h>

struct SmallImage {
  int w = 0, h = 0;
  uint8_t *rgba = nullptr;   // w * h * 4 bytes, straight (not premultiplied) alpha; free() it
};

// Decodes png[0..len) and scales it to fit within boxW x boxH (never enlarges past 2x).
// Fully transparent borders are trimmed first so the logo fills the box.
// maxRaw caps the memory used while decoding. Returns nullptr on success, or a short reason.
const char *pngDecodeFit(const uint8_t *png, size_t len, int boxW, int boxH, SmallImage &out, size_t maxRaw = 70000);

// zlib inflate into a buffer of exactly dstLen bytes; returns bytes written, or 0 on error.
// Implemented per platform (pngsmall.cpp uses the ESP32's ROM inflate; the PC tests use zlib).
size_t pngInflate(uint8_t *dst, size_t dstLen, const uint8_t *src, size_t srcLen);
