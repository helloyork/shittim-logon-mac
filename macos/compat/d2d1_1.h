// shittim-logon / macos / compat / d2d1_1.h
//
// The D2D surface the shared headers touch: a bitmap made from a DXGI surface (on
// the Mac, a gpu::Target's texture -- the host reads it back out of the bitmap) and
// a bitmap made from CPU pixels, for the software path.

#pragma once

#include <vector>
#include "windows.h"

enum D2D1_BITMAP_OPTIONS {
    D2D1_BITMAP_OPTIONS_NONE = 0,
    D2D1_BITMAP_OPTIONS_TARGET = 1,
    D2D1_BITMAP_OPTIONS_CANNOT_DRAW = 2,
};
inline D2D1_BITMAP_OPTIONS operator|(D2D1_BITMAP_OPTIONS a, D2D1_BITMAP_OPTIONS b) {
    return (D2D1_BITMAP_OPTIONS)((int)a | (int)b);
}
enum D2D1_ALPHA_MODE { D2D1_ALPHA_MODE_UNKNOWN = 0, D2D1_ALPHA_MODE_PREMULTIPLIED = 1 };

struct D2D1_PIXEL_FORMAT { DXGI_FORMAT format; D2D1_ALPHA_MODE alphaMode; };
struct D2D1_BITMAP_PROPERTIES1 {
    D2D1_PIXEL_FORMAT pixelFormat;
    float dpiX, dpiY;
    D2D1_BITMAP_OPTIONS bitmapOptions;
    void *colorContext;
};
struct D2D1_SIZE_U { UINT32 width, height; };
struct D2D1_RECT_U { UINT32 left, top, right, bottom; };

namespace D2D1 {
inline D2D1_PIXEL_FORMAT PixelFormat(DXGI_FORMAT f, D2D1_ALPHA_MODE a) { return { f, a }; }
inline D2D1_BITMAP_PROPERTIES1 BitmapProperties1(D2D1_BITMAP_OPTIONS o, D2D1_PIXEL_FORMAT pf) {
    return { pf, 96.0f, 96.0f, o, nullptr };
}
inline D2D1_SIZE_U SizeU(UINT32 w, UINT32 h) { return { w, h }; }
inline D2D1_RECT_U RectU(UINT32 l, UINT32 t, UINT32 r, UINT32 b) { return { l, t, r, b }; }
}

struct ID2D1Bitmap1 : IUnknown {
    IDXGISurface *surface = nullptr;      // retained; the GPU path's target
    UINT32 width = 0, height = 0;
    std::vector<uint8_t> cpu;             // the software path's pixels, BGRA premultiplied
    ~ID2D1Bitmap1() override { if (surface) surface->Release(); }
    HRESULT CopyFromMemory(const D2D1_RECT_U *, const void *src, UINT32 pitch) {
        if (!src) return E_FAIL;
        cpu.assign((const uint8_t *)src, (const uint8_t *)src + size_t(pitch) * height);
        return S_OK;
    }
};

struct ID2D1DeviceContext : IUnknown {
    HRESULT CreateBitmapFromDxgiSurface(IDXGISurface *s, const D2D1_BITMAP_PROPERTIES1 *, ID2D1Bitmap1 **out) {
        if (!s || !out) return E_FAIL;
        auto *b = new ID2D1Bitmap1();
        b->surface = s;
        s->AddRef();
        *out = b;
        return S_OK;
    }
    HRESULT CreateBitmap(D2D1_SIZE_U size, const void *src, UINT32 pitch, const D2D1_BITMAP_PROPERTIES1 &,
                         ID2D1Bitmap1 **out) {
        if (!out) return E_FAIL;
        auto *b = new ID2D1Bitmap1();
        b->width = size.width; b->height = size.height;
        if (src) b->cpu.assign((const uint8_t *)src, (const uint8_t *)src + size_t(pitch) * size.height);
        *out = b;
        return S_OK;
    }
};
