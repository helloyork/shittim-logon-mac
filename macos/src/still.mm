// shittim-logon / macos / src / still.mm
//
// Plays the wake sequence offscreen through the real SpineLayer and the Metal gpu.h,
// and writes the frame at chosen moments to PNG. No window, no lock screen: this is
// the check that the Mac renders what Windows renders, before any of it goes near
// the lock screen.
//
//   still <assetDir> <scene> <outDir> [width height]
//
// Timeline (seconds from start): the wake is triggered at 3.0, and frames are
// written at the times in kShots, which cover idle, the reaction, both flashes and
// the settled foreground.

#include "host.h"

#import <Foundation/Foundation.h>

int main(int argc, char **argv) {
    @autoreleasepool {
        setlocale(LC_ALL, "en_US.UTF-8");
        if (argc < 4) { std::fprintf(stderr, "usage: still <assetDir> <scene> <outDir> [w h]\n"); return 2; }
        const std::string dir = argv[1], sceneName = argv[2], out = argv[3];
        const int W = argc > 5 ? std::atoi(argv[4]) : 1920;
        const int H = argc > 5 ? std::atoi(argv[5]) : 1080;

        auto *dev = new ID3D11Device();
        if (!dev->Init()) { host::Log(L"no Metal device"); return 1; }
        auto *d2d = new ID2D1DeviceContext();

        auto layer = std::make_unique<SpineLayer>();
        std::string character;
        if (!host::ApplyScene(*layer, sceneName, character)) return 1;
        if (!layer->Load(dir, character, W, H, host::Log)) { host::Log(L"load failed"); return 1; }
        if (!layer->InitGpu(dev, dev, d2d, W, H)) { host::Log(L"InitGpu failed"); return 1; }

        const float kTrigger = 3.0f;
        // Shot times may be given after the size; 0.05 is the empty room, before the
        // arrival flash brings its people in.
        std::vector<float> kShots = { 1.0f, 2.9f, 3.3f, 3.6f, 3.9f, 4.2f, 4.6f, 5.2f, 7.0f };
        if (argc > 6) { kShots.clear(); for (int i = 6; i < argc; i++) kShots.push_back((float)std::atof(argv[i])); }
        const float dt = 1.0f / 60.0f;
        size_t next = 0;
        bool triggered = false;
        for (int f = 0; f < 60 * 8 && next < kShots.size(); f++) {
            const float t = f * dt;
            if (!triggered && t >= kTrigger) { layer->Trigger(); triggered = true; }
            layer->Step(dt);
            ID2D1Bitmap1 *bmp = layer->RenderGpu(dev, dev);
            const bool shoot = t + 1e-4f >= kShots[next];
            dev->Flush(shoot);
            if (!shoot || !bmp) continue;
            auto *surf = dynamic_cast<ID3D11Texture2D *>(bmp->surface);
            const int w = layer->resolveRT.w, h = layer->resolveRT.h;
            std::vector<uint8_t> bgra(size_t(w) * h * 4);
            [surf->tex getBytes:bgra.data() bytesPerRow:(NSUInteger)w * 4 fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
            // Premultiplied BGRA over a mid-grey checker, so transparency shows.
            sl::Image img; img.width = w; img.height = h; img.rgba.resize(size_t(w) * h * 4);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    const size_t p = (size_t(y) * w + x) * 4;
                    const float a = bgra[p + 3] / 255.0f;
                    const float bg = (((x / 32) + (y / 32)) & 1) ? 0.35f : 0.45f;
                    for (int c = 0; c < 3; c++) {
                        const float v = bgra[p + 2 - c] / 255.0f + bg * (1.0f - a);
                        img.rgba[p + c] = (uint8_t)std::min(255.0f, v * 255.0f + 0.5f);
                    }
                    img.rgba[p + 3] = 255;
                }
            char name[64];
            std::snprintf(name, sizeof(name), "/%s-t%04.1f.png", sceneName.c_str(), kShots[next]);
            sl::savePng(img, out + name);
            host::Log(L"shot %.1fs phase %d batches %d triangles %d -> %hs", kShots[next], (int)layer->phase,
                      (int)layer->lastBatches, (int)layer->lastTriangles, name + 1);
            next++;
        }
        return 0;
    }
}
