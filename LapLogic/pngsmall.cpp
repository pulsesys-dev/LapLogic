#include "pngsmall.h"
#include <stdlib.h>
#include <string.h>

static inline int max_int(int a, int b) { return a > b ? a : b; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static uint8_t paeth(int a, int b, int c) {
  const int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
  return (uint8_t)(pa <= pb && pa <= pc ? a : (pb <= pc ? b : c));
}

const char *pngDecodeFit(const uint8_t *png, size_t len, int boxW, int boxH, SmallImage &out, size_t maxRaw) {
  static const uint8_t SIG[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  out = SmallImage();
  if (len < 8 + 25 || memcmp(png, SIG, 8)) return "not a PNG file";

  // ---- header + chunks
  int w = 0, h = 0, depth = 0, type = 0, interlace = 0;
  uint8_t plte[256][3];
  uint8_t trns[256];
  int nPlte = 0;
  bool hasTrnsKey = false;
  uint16_t trnsKey[3] = {0, 0, 0};
  memset(trns, 255, sizeof(trns));
  size_t idatTotal = 0;
  for (size_t pos = 8; pos + 12 <= len;) {
    const uint32_t clen = be32(png + pos);
    const uint8_t *ct = png + pos + 4, *cd = png + pos + 8;
    if (clen > len - pos - 12) return "file is cut short";
    if (!memcmp(ct, "IHDR", 4) && clen >= 13) {
      w = (int)be32(cd); h = (int)be32(cd + 4); depth = cd[8]; type = cd[9]; interlace = cd[12];
    } else if (!memcmp(ct, "PLTE", 4)) {
      nPlte = (int)(clen / 3 > 256 ? 256 : clen / 3);
      for (int i = 0; i < nPlte; i++) memcpy(plte[i], cd + i * 3, 3);
    } else if (!memcmp(ct, "tRNS", 4)) {
      if (type == 3) for (uint32_t i = 0; i < clen && i < 256; i++) trns[i] = cd[i];
      else if (type == 0 && clen >= 2) { hasTrnsKey = true; trnsKey[0] = (uint16_t)(cd[0] << 8 | cd[1]); }
      else if (type == 2 && clen >= 6) { hasTrnsKey = true; for (int k = 0; k < 3; k++) trnsKey[k] = (uint16_t)(cd[k * 2] << 8 | cd[k * 2 + 1]); }
    } else if (!memcmp(ct, "IDAT", 4)) {
      idatTotal += clen;
    } else if (!memcmp(ct, "IEND", 4)) break;
    pos += 12 + clen;
  }
  if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return "bad image size";
  if (interlace) return "interlaced PNG - re-save it without interlacing";
  int ch;   // channels per pixel
  switch (type) {
    case 0: ch = 1; break;
    case 2: ch = 3; break;
    case 3: ch = 1; break;
    case 4: ch = 2; break;
    case 6: ch = 4; break;
    default: return "unknown PNG colour type";
  }
  if ((type == 3 || type == 0) ? (depth != 1 && depth != 2 && depth != 4 && depth != 8) : depth != 8)
    return depth == 16 ? "16-bit PNG - re-save it as 8-bit" : "unsupported PNG bit depth";
  if (type == 3 && !nPlte) return "palette missing";
  if (!idatTotal) return "no image data";

  const size_t stride = ((size_t)w * ch * depth + 7) / 8;
  const size_t rawLen = (stride + 1) * (size_t)h;
  if (rawLen > maxRaw) return "image too large - use 128 x 128 pixels or smaller";

  // ---- gather the compressed data and inflate it
  uint8_t *z = (uint8_t *)malloc(idatTotal);
  uint8_t *raw = (uint8_t *)malloc(rawLen);
  if (!z || !raw) { free(z); free(raw); return "out of memory"; }
  size_t zl = 0;
  for (size_t pos = 8; pos + 12 <= len;) {
    const uint32_t clen = be32(png + pos);
    if (!memcmp(png + pos + 4, "IDAT", 4)) { memcpy(z + zl, png + pos + 8, clen); zl += clen; }
    if (!memcmp(png + pos + 4, "IEND", 4)) break;
    pos += 12 + clen;
  }
  const size_t got = pngInflate(raw, rawLen, z, zl);
  free(z);
  if (got != rawLen) { free(raw); return "couldn't decompress the image"; }

  // ---- undo the per-row filters, in place
  const int bpp = (ch * depth + 7) / 8;   // bytes per complete pixel, for the filters
  for (int y = 0; y < h; y++) {
    uint8_t *row = raw + y * (stride + 1);
    const uint8_t f = row[0];
    uint8_t *cur = row + 1;
    const uint8_t *prev = y ? raw + (y - 1) * (stride + 1) + 1 : nullptr;
    for (size_t i = 0; i < stride; i++) {
      const int a = i >= (size_t)bpp ? cur[i - bpp] : 0, b = prev ? prev[i] : 0, c = (prev && i >= (size_t)bpp) ? prev[i - bpp] : 0;
      switch (f) {
        case 0: break;
        case 1: cur[i] += a; break;
        case 2: cur[i] += b; break;
        case 3: cur[i] += (uint8_t)((a + b) >> 1); break;
        case 4: cur[i] += paeth(a, b, c); break;
        default: free(raw); return "corrupt image data";
      }
    }
  }

  // ---- read one source pixel as RGBA
  auto pixel = [&](int x, int y, uint8_t px[4]) {
    const uint8_t *row = raw + y * (stride + 1) + 1;
    if (type == 3) {
      const int bitPos = x * depth;
      const int idx = (row[bitPos >> 3] >> (8 - depth - (bitPos & 7))) & ((1 << depth) - 1);
      const uint8_t *c = idx < nPlte ? plte[idx] : plte[0];
      px[0] = c[0]; px[1] = c[1]; px[2] = c[2]; px[3] = trns[idx];
      return;
    }
    if (type == 0 && depth < 8) {   // 1, 2 or 4-bit greyscale
      const int bitPos = x * depth;
      const int v = (row[bitPos >> 3] >> (8 - depth - (bitPos & 7))) & ((1 << depth) - 1);
      px[0] = px[1] = px[2] = (uint8_t)(v * 255 / ((1 << depth) - 1));
      px[3] = (hasTrnsKey && v == trnsKey[0]) ? 0 : 255;
      return;
    }
    const uint8_t *p = row + x * ch;
    switch (type) {
      case 0: px[0] = px[1] = px[2] = p[0]; px[3] = (hasTrnsKey && p[0] == trnsKey[0]) ? 0 : 255; break;
      case 4: px[0] = px[1] = px[2] = p[0]; px[3] = p[1]; break;
      case 2: px[0] = p[0]; px[1] = p[1]; px[2] = p[2];
              px[3] = (hasTrnsKey && p[0] == trnsKey[0] && p[1] == trnsKey[1] && p[2] == trnsKey[2]) ? 0 : 255; break;
      default: memcpy(px, p, 4); break;
    }
  };

  // ---- trim fully transparent borders
  int x0 = w, y0 = h, x1 = -1, y1 = -1;
  uint8_t px[4];
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      pixel(x, y, px);
      if (px[3] > 8) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    }
  if (x1 < 0) { free(raw); return "the image is completely transparent"; }
  const int sw = x1 - x0 + 1, sh = y1 - y0 + 1;

  // ---- fit the box, keeping the shape
  int tw = boxW, th = (int)((int64_t)sh * boxW / sw);
  if (th > boxH) { th = boxH; tw = (int)((int64_t)sw * boxH / sh); }
  if (tw > sw * 2) { tw = sw * 2; th = sh * 2; }   // tiny source: don't blow it up
  if (tw < 1) tw = 1;
  if (th < 1) th = 1;

  out.rgba = (uint8_t *)malloc((size_t)tw * th * 4);
  if (!out.rgba) { free(raw); return "out of memory"; }
  out.w = tw; out.h = th;

  // ---- box filter: average every source pixel under each target pixel (alpha-weighted)
  for (int ty = 0; ty < th; ty++) {
    const int sy0 = y0 + ty * sh / th, sy1 = y0 + max_int((ty + 1) * sh / th, ty * sh / th + 1);
    for (int tx = 0; tx < tw; tx++) {
      const int sx0 = x0 + tx * sw / tw, sx1 = x0 + max_int((tx + 1) * sw / tw, tx * sw / tw + 1);
      uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
      for (int y = sy0; y < sy1; y++)
        for (int x = sx0; x < sx1; x++) {
          pixel(x, y, px);
          r += px[0] * px[3]; g += px[1] * px[3]; b += px[2] * px[3]; a += px[3]; n++;
        }
      uint8_t *o = out.rgba + (ty * tw + tx) * 4;
      if (a) { o[0] = (uint8_t)(r / a); o[1] = (uint8_t)(g / a); o[2] = (uint8_t)(b / a); }
      else o[0] = o[1] = o[2] = 0;
      o[3] = (uint8_t)(a / n);
    }
  }
  free(raw);
  return nullptr;
}

#if defined(ARDUINO)
// The ESP32's ROM includes miniz's inflate; its state (~11 KB) goes on the heap, not the stack.
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  #include "esp32s3/rom/miniz.h"
#else
  #include "esp32/rom/miniz.h"
#endif
size_t pngInflate(uint8_t *dst, size_t dstLen, const uint8_t *src, size_t srcLen) {
  tinfl_decompressor *d = (tinfl_decompressor *)malloc(sizeof(tinfl_decompressor));
  if (!d) return 0;
  tinfl_init(d);
  size_t inLen = srcLen, outLen = dstLen;
  const tinfl_status st = tinfl_decompress(d, src, &inLen, dst, dst, &outLen,
                                           TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  free(d);
  return (st == TINFL_STATUS_DONE || st == TINFL_STATUS_HAS_MORE_OUTPUT) ? outLen : 0;
}
#endif
