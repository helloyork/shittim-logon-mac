// shittim-logon / macos / replace / render/src/image.h
//
// macOS stand-in for render/src/image.h: ImageIO instead of WIC. Same struct, same
// three functions. Straight alpha in and out, as the original insists: CoreGraphics
// only draws into premultiplied bitmaps, so PNGs are decoded through
// CGImageGetDataProvider when the source is already 8-bit RGBA/RGB (no premultiply
// round trip), and through a premultiplied context plus an exact un-premultiply only
// as a fallback.

#pragma once

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "pathio.h"

namespace sl {

struct Image {
    int width  = 0;
    int height = 0;
    std::vector<uint8_t> rgba;   // straight alpha, 4 bytes per pixel, row-major

    const uint8_t* pixel(int x, int y) const { return &rgba[(size_t(y) * width + x) * 4]; }
    uint8_t*       pixel(int x, int y)       { return &rgba[(size_t(y) * width + x) * 4]; }
};

namespace detail {
inline CFURLRef url(const std::string &path) {
    return CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8 *)path.c_str(), (CFIndex)path.size(), false);
}
}

inline Image loadPng(const std::string& path) {
    CFURLRef u = detail::url(path);
    CGImageSourceRef src = u ? CGImageSourceCreateWithURL(u, nullptr) : nullptr;
    if (u) CFRelease(u);
    if (!src) throw std::runtime_error("could not open " + path);
    CGImageRef cg = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    CFRelease(src);
    if (!cg) throw std::runtime_error("could not decode " + path);

    Image img;
    img.width  = (int)CGImageGetWidth(cg);
    img.height = (int)CGImageGetHeight(cg);
    img.rgba.assign(size_t(img.width) * img.height * 4, 0);

    // Fast, exact path: 8 bits per component, RGB(A) order, straight or no alpha.
    const CGImageAlphaInfo ai = (CGImageAlphaInfo)(CGImageGetBitmapInfo(cg) & kCGBitmapAlphaInfoMask);
    const bool order32 = (CGImageGetBitmapInfo(cg) & kCGBitmapByteOrderMask) == kCGBitmapByteOrderDefault ||
                         (CGImageGetBitmapInfo(cg) & kCGBitmapByteOrderMask) == kCGBitmapByteOrder32Big;
    const size_t bpc = CGImageGetBitsPerComponent(cg), bpp = CGImageGetBitsPerPixel(cg);
    bool done = false;
    if (bpc == 8 && order32 && (ai == kCGImageAlphaLast || ai == kCGImageAlphaNone || ai == kCGImageAlphaNoneSkipLast) &&
        (bpp == 32 || bpp == 24)) {
        CFDataRef data = CGDataProviderCopyData(CGImageGetDataProvider(cg));
        if (data) {
            const uint8_t *p = CFDataGetBytePtr(data);
            const size_t stride = CGImageGetBytesPerRow(cg), n = bpp / 8;
            for (int y = 0; y < img.height; y++) {
                const uint8_t *row = p + stride * y;
                uint8_t *out = &img.rgba[size_t(y) * img.width * 4];
                for (int x = 0; x < img.width; x++) {
                    out[x * 4 + 0] = row[x * n + 0];
                    out[x * 4 + 1] = row[x * n + 1];
                    out[x * 4 + 2] = row[x * n + 2];
                    out[x * 4 + 3] = (n == 4 && ai == kCGImageAlphaLast) ? row[x * n + 3] : 255;
                }
            }
            CFRelease(data);
            done = true;
        }
    }
    if (!done) {
        // Fallback: draw premultiplied, then un-premultiply.
        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef ctx = CGBitmapContextCreate(img.rgba.data(), img.width, img.height, 8, size_t(img.width) * 4, cs,
                                                 kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        CGColorSpaceRelease(cs);
        if (!ctx) { CGImageRelease(cg); throw std::runtime_error("no bitmap context for " + path); }
        CGContextSetBlendMode(ctx, kCGBlendModeCopy);
        CGContextDrawImage(ctx, CGRectMake(0, 0, img.width, img.height), cg);
        CGContextRelease(ctx);
        for (size_t i = 0; i < img.rgba.size(); i += 4) {
            const uint8_t a = img.rgba[i + 3];
            if (a && a != 255) for (int c = 0; c < 3; c++) img.rgba[i + c] = (uint8_t)std::min(255, (img.rgba[i + c] * 255 + a / 2) / a);
        }
    }
    CGImageRelease(cg);
    return img;
}

inline void writeImage(const Image& img, const std::string& path, CFStringRef type, float quality) {
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CFDataRef data = CFDataCreate(nullptr, img.rgba.data(), (CFIndex)img.rgba.size());
    CGDataProviderRef prov = CGDataProviderCreateWithCFData(data);
    CGImageRef cg = CGImageCreate(img.width, img.height, 8, 32, size_t(img.width) * 4, cs,
                                  kCGImageAlphaLast | kCGBitmapByteOrder32Big, prov, nullptr, false,
                                  kCGRenderingIntentDefault);
    CFURLRef u = detail::url(path);
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL(u, type, 1, nullptr);
    if (!dst) { CGImageRelease(cg); CFRelease(u); throw std::runtime_error("could not create " + path); }
    CFNumberRef q = CFNumberCreate(nullptr, kCFNumberFloatType, &quality);
    const void *k[] = { kCGImageDestinationLossyCompressionQuality };
    const void *v[] = { q };
    CFDictionaryRef props = CFDictionaryCreate(nullptr, k, v, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CGImageDestinationAddImage(dst, cg, props);
    const bool ok = CGImageDestinationFinalize(dst);
    CFRelease(props); CFRelease(q); CFRelease(dst); CFRelease(u);
    CGImageRelease(cg); CGDataProviderRelease(prov); CFRelease(data); CGColorSpaceRelease(cs);
    if (!ok) throw std::runtime_error("could not write " + path);
}

inline void savePng(const Image& img, const std::string& path) { writeImage(img, path, CFSTR("public.png"), 1.0f); }

inline constexpr float kStillQuality = 0.95f;
inline void saveJpeg(const Image& img, const std::string& path, float quality = kStillQuality) {
    writeImage(img, path, CFSTR("public.jpeg"), quality);
}

} // namespace sl
