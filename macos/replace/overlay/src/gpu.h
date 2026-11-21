// shittim-logon / macos / replace / overlay/src/gpu.h
//
// Metal stand-in for overlay/src/gpu.h. Same namespace, same types, same member
// names, same draw primitives, so overlay/src/spine_layer.h and gpu_scene.h compile
// against it unchanged. Only usable from Objective-C++ (.mm) with ARC.
//
// How D3D's immediate model maps onto Metal's encoders:
//   BindTarget  ends the open render encoder if the target changes
//   Clear       makes the next encoder on that target start with a clear (and runs
//               an empty clearing pass if nothing is drawn before the next switch)
//   Draw*       open a render encoder on the bound target lazily
//   Texture::Create with mips, and ReadbackAlpha, end the encoder first because a
//               command buffer has one encoder open at a time
// The command buffer is committed by the host at the end of each frame (Device::Flush).
//
// Formats: A8_UNORM is not renderable on Apple GPUs, so "A8" targets are RGBA8 and
// every shader that reads them reads .a, exactly as on Windows.

#pragma once

#import <Metal/Metal.h>

#include <windows.h>
#include <d2d1_1.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

// ------------------------------------------------------------- D3D stand-ins

struct ID3D11Texture2D : IDXGISurface {
    id<MTLTexture> tex = nil;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
};

struct ID3D11ShaderResourceView : IUnknown {
    id<MTLTexture> tex = nil;
};

struct ID3D11PixelShader : IUnknown {
    std::string name;
    id<MTLFunction> fn = nil;
};
typedef ID3D11PixelShader ID3D11VertexShader;

// One object plays both device and immediate context.
struct ID3D11Device : IUnknown {
    id<MTLDevice> dev = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLCommandBuffer> cmd = nil;
    id<MTLRenderCommandEncoder> enc = nil;
    id<MTLTexture> bound = nil;
    bool pendingClear = false;
    MTLClearColor clearColor = MTLClearColorMake(0, 0, 0, 0);

    bool Init() {
        dev = MTLCreateSystemDefaultDevice();
        if (!dev) return false;
        queue = [dev newCommandQueue];
        return queue != nil;
    }
    id<MTLCommandBuffer> Cmd() {
        if (!cmd) cmd = [queue commandBuffer];
        return cmd;
    }
    // Closes the open encoder; runs a pending clear that no draw consumed.
    void EndEncoder() {
        if (enc) { [enc endEncoding]; enc = nil; }
        else if (pendingClear && bound) {
            MTLRenderPassDescriptor *d = [MTLRenderPassDescriptor renderPassDescriptor];
            d.colorAttachments[0].texture = bound;
            d.colorAttachments[0].loadAction = MTLLoadActionClear;
            d.colorAttachments[0].clearColor = clearColor;
            d.colorAttachments[0].storeAction = MTLStoreActionStore;
            [[Cmd() renderCommandEncoderWithDescriptor:d] endEncoding];
        }
        pendingClear = false;
    }
    void Bind(id<MTLTexture> t) {
        if (t == bound) return;
        EndEncoder();
        bound = t;
    }
    void ClearBound(float r, float g, float b, float a) {
        if (enc) { [enc endEncoding]; enc = nil; }
        pendingClear = true;
        clearColor = MTLClearColorMake(r, g, b, a);
    }
    id<MTLRenderCommandEncoder> Enc() {
        if (!enc) {
            MTLRenderPassDescriptor *d = [MTLRenderPassDescriptor renderPassDescriptor];
            d.colorAttachments[0].texture = bound;
            d.colorAttachments[0].loadAction = pendingClear ? MTLLoadActionClear : MTLLoadActionLoad;
            d.colorAttachments[0].clearColor = clearColor;
            d.colorAttachments[0].storeAction = MTLStoreActionStore;
            enc = [Cmd() renderCommandEncoderWithDescriptor:d];
            pendingClear = false;
        }
        return enc;
    }
    // Commits everything recorded so far. `wait` blocks until the GPU is done.
    void Flush(bool wait) {
        EndEncoder();
        bound = nil;
        if (cmd) {
            [cmd commit];
            if (wait) [cmd waitUntilCompleted];
            cmd = nil;
        }
    }
};
typedef ID3D11Device ID3D11DeviceContext;

// ------------------------------------------------------------------ shaders
//
// overlay/shaders/overlay.hlsl, transcribed to MSL pass for pass. The cbuffer has
// the same layout (float2, float2, float4 x3 = 64 bytes).

namespace gpu {
inline const char *kShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct CB { float2 invTarget; float2 texel; float4 tint; float4 params; float4 dim; };
struct Vtx { packed_float2 pos; packed_float2 uv; packed_float4 col; };
struct VSOut { float4 pos [[position]]; float2 uv; float4 col; };
struct FSOut { float4 pos [[position]]; float2 uv; };

vertex VSOut VSSprite(const device Vtx *v [[buffer(0)]], constant CB &cb [[buffer(1)]], uint id [[vertex_id]]) {
    VSOut o;
    float2 p = float2(v[id].pos);
    o.pos = float4(p.x * cb.invTarget.x * 2.0 - 1.0, 1.0 - p.y * cb.invTarget.y * 2.0, 0.0, 1.0);
    o.uv = float2(v[id].uv);
    o.col = float4(v[id].col);
    return o;
}
fragment float4 PSSprite(VSOut i [[stage_in]], texture2d<float> texA [[texture(0)]], sampler smp [[sampler(0)]]) {
    return texA.sample(smp, i.uv) * i.col;
}
fragment float4 PSSolid(VSOut i [[stage_in]]) { return i.col; }
fragment float4 PSSpriteLum(VSOut i [[stage_in]], texture2d<float> texA [[texture(0)]], sampler smp [[sampler(0)]]) {
    float3 t = texA.sample(smp, i.uv).rgb;
    float lum = (t.r + t.g + t.b) * (1.0 / 3.0);
    return float4(i.col.rgb, lum * i.col.a);
}
fragment float4 PSSpriteDimmed(VSOut i [[stage_in]], constant CB &cb [[buffer(0)]],
                               texture2d<float> texA [[texture(0)]], sampler smp [[sampler(0)]]) {
    float4 c = texA.sample(smp, i.uv) * i.col;
    c.rgb = max(0.0, c.rgb * cb.dim.rgb + cb.dim.a);
    return c;
}
fragment float4 PSSpriteDimmedMasked(VSOut i [[stage_in]], constant CB &cb [[buffer(0)]],
                                     texture2d<float> texA [[texture(0)]], texture2d<float> texB [[texture(1)]],
                                     sampler smp [[sampler(0)]]) {
    float4 c = texA.sample(smp, i.uv) * i.col;
    c.rgb = max(0.0, c.rgb * cb.dim.rgb + cb.dim.a);
    float occl = texB.sample(smp, i.pos.xy * cb.invTarget).a;
    c.a *= saturate(1.0 - occl);
    return c;
}
fragment float4 PSSpriteDimmedMaskedPremul(VSOut i [[stage_in]], constant CB &cb [[buffer(0)]],
                                           texture2d<float> texA [[texture(0)]], texture2d<float> texB [[texture(1)]],
                                           sampler smp [[sampler(0)]]) {
    float4 c = texA.sample(smp, i.uv) * i.col;
    float occl = saturate(1.0 - texB.sample(smp, i.pos.xy * cb.invTarget).a);
    c.rgb = max(0.0, c.rgb * cb.dim.rgb + cb.dim.a * c.a);
    return c * occl;
}
vertex FSOut VSFullscreen(uint id [[vertex_id]]) {
    FSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv = uv;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}
fragment float4 PSGlow(FSOut i [[stage_in]], constant CB &cb [[buffer(0)]],
                       texture2d<float> texA [[texture(0)]], sampler smp [[sampler(0)]]) {
    float a = texA.sample(smp, i.uv).a;
    return float4(cb.tint.rgb * a, 0.0);
}
fragment float4 PSDownThreshold(FSOut i [[stage_in]], constant CB &cb [[buffer(0)]],
                                texture2d<float> texA [[texture(0)]], sampler smp [[sampler(0)]]) {
    float2 o = cb.texel;
    float3 s = texA.sample(smp, i.uv + float2(-o.x, -o.y)).rgb + texA.sample(smp, i.uv + float2( o.x, -o.y)).rgb
             + texA.sample(smp, i.uv + float2(-o.x,  o.y)).rgb + texA.sample(smp, i.uv + float2( o.x,  o.y)).rgb;
    return float4(max(0.0, s * 0.25 - cb.params.x), 0.0);
}
fragment float4 PSDownsample(FSOut i [[stage_in]], constant CB &cb [[buffer(0)]],
                             texture2d<float> texA [[texture(0)]], sampler smp [[sampler(0)]]) {
    float2 o = cb.texel * 0.5;
    float3 s = texA.sample(smp, i.uv + float2(-o.x, -o.y)).rgb + texA.sample(smp, i.uv + float2( o.x, -o.y)).rgb
             + texA.sample(smp, i.uv + float2(-o.x,  o.y)).rgb + texA.sample(smp, i.uv + float2( o.x,  o.y)).rgb;
    return float4(s * 0.25, 0.0);
}
fragment float4 PSBlur(FSOut i [[stage_in]], constant CB &cb [[buffer(0)]],
                       texture2d<float> texA [[texture(0)]], sampler smp [[sampler(0)]]) {
    float2 t = cb.texel;
    float3 s = texA.sample(smp, i.uv - t * 2.0).rgb * 0.0625 + texA.sample(smp, i.uv - t).rgb * 0.25
             + texA.sample(smp, i.uv).rgb * 0.375 + texA.sample(smp, i.uv + t).rgb * 0.25
             + texA.sample(smp, i.uv + t * 2.0).rgb * 0.0625;
    return float4(s, 0.0);
}
fragment float4 PSUpsample(FSOut i [[stage_in]], constant CB &cb [[buffer(0)]],
                           texture2d<float> texA [[texture(0)]], sampler smp [[sampler(0)]]) {
    return float4(texA.sample(smp, i.uv).rgb * cb.params.y, 0.0);
}
fragment float4 PSBlit(FSOut i [[stage_in]], texture2d<float> texA [[texture(0)]], sampler smp [[sampler(0)]]) {
    return texA.sample(smp, i.uv);
}
fragment float4 PSResolve(FSOut i [[stage_in]], texture2d<float> texA [[texture(0)]], texture2d<float> texB [[texture(1)]],
                          sampler smp [[sampler(0)]]) {
    float4 s = texA.sample(smp, i.uv);
    float3 f = texB.sample(smp, i.uv).rgb;
    return float4(saturate(s.rgb + f), saturate(s.a));
}
)MSL";

using Microsoft::WRL::ComPtr;

struct CB {
    float invTarget[2] = { 0, 0 };
    float texel[2]     = { 0, 0 };
    float tint[4]      = { 0, 0, 0, 0 };
    float params[4]    = { 0, 0, 0, 0 };
    float dim[4]       = { 1, 1, 1, 0 };
};
static_assert(sizeof(CB) == 64, "CB layout must match the shader");

struct Vertex {
    float x, y;
    float u, v;
    float r, g, b, a;
};
static_assert(sizeof(Vertex) == 32, "Vertex layout must match the shader");

inline MTLPixelFormat MetalFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return MTLPixelFormatRGBA16Float;
    case DXGI_FORMAT_B8G8R8A8_UNORM:     return MTLPixelFormatBGRA8Unorm;
    case DXGI_FORMAT_A8_UNORM:           // not renderable on Apple GPUs; .a of RGBA8
    case DXGI_FORMAT_R8G8B8A8_UNORM:     return MTLPixelFormatRGBA8Unorm;
    default:                             return MTLPixelFormatInvalid;
    }
}

struct Texture {
    ComPtr<ID3D11Texture2D>          tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    int w = 0, h = 0;

    bool Create(ID3D11Device *dev, const uint8_t *rgba, int width, int height, bool mips = true) {
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                     width:width height:height
                                                                                 mipmapped:mips ? YES : NO];
        d.usage = MTLTextureUsageShaderRead;
        d.storageMode = MTLStorageModeShared;
        id<MTLTexture> t = [dev->dev newTextureWithDescriptor:d];
        if (!t) return false;
        [t replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0 withBytes:rgba bytesPerRow:(NSUInteger)width * 4];
        if (mips) {
            dev->EndEncoder();
            id<MTLBlitCommandEncoder> b = [dev->Cmd() blitCommandEncoder];
            [b generateMipmapsForTexture:t];
            [b endEncoding];
        }
        auto *tx = new ID3D11Texture2D(); tx->tex = t; tx->fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
        auto *sv = new ID3D11ShaderResourceView(); sv->tex = t;
        tex.Attach(tx); srv.Attach(sv);
        w = width; h = height;
        return true;
    }
};

struct Target {
    ComPtr<ID3D11Texture2D>          tex;
    ComPtr<ID3D11ShaderResourceView> rtv;   // unused on the Mac; kept for shape
    ComPtr<ID3D11ShaderResourceView> srv;
    int w = 0, h = 0;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;

    bool Create(ID3D11Device *dev, int width, int height, DXGI_FORMAT format) {
        tex.Reset(); rtv.Reset(); srv.Reset();
        const MTLPixelFormat pf = MetalFormat(format);
        if (pf == MTLPixelFormatInvalid || width <= 0 || height <= 0) return false;
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pf
                                                                                     width:width height:height
                                                                                 mipmapped:NO];
        d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        // Shared where the CPU reads or writes it (the silhouette readback, the clock
        // upload, the frame handed to the host); private for the float16 scene.
        d.storageMode = format == DXGI_FORMAT_R16G16B16A16_FLOAT ? MTLStorageModePrivate : MTLStorageModeShared;
        id<MTLTexture> t = [dev->dev newTextureWithDescriptor:d];
        if (!t) return false;
        auto *tx = new ID3D11Texture2D(); tx->tex = t; tx->fmt = format;
        auto *sv = new ID3D11ShaderResourceView(); sv->tex = t;
        tex.Attach(tx); srv.Attach(sv);
        w = width; h = height; fmt = format;
        return true;
    }
};

enum class Blend { Normal, Premul, Additive, Multiply, Screen, Light, LightAlpha, Opaque, Erase };

struct Renderer {
    ID3D11Device        *dev = nullptr;
    ID3D11DeviceContext *ctx = nullptr;

    ComPtr<ID3D11VertexShader> vsSprite, vsFull;
    ComPtr<ID3D11PixelShader>  psSprite, psSolid, psSpriteLum, psSpriteDim, psSpriteDimMasked, psSpriteDimMaskedPre,
                               psGlow, psDownT, psDown, psBlur, psUp, psResolve, psBlit;
    id<MTLLibrary> lib = nil;
    id<MTLSamplerState> samp = nil;
    std::map<std::tuple<void *, void *, int, int>, id<MTLRenderPipelineState>> pipes;

    // Per-draw state, as D3D keeps it.
    CB cb{};
    ID3D11PixelShader *curPS = nullptr;
    id<MTLTexture> mask = nil;
    id<MTLBuffer> vb = nil, ib = nil;

    bool Init(ID3D11Device *device, ID3D11DeviceContext *context, void (*log)(const wchar_t *, ...)) {
        dev = device; ctx = context;
        NSError *err = nil;
        lib = [dev->dev newLibraryWithSource:[NSString stringWithUTF8String:kShaderSource] options:nil error:&err];
        if (!lib) {
            log(L"gpu: shader library failed: %hs", err ? err.localizedDescription.UTF8String : "?");
            return false;
        }
        struct { const char *name; ComPtr<ID3D11PixelShader> *out; } fns[] = {
            { "VSSprite", std::addressof(vsSprite) }, { "VSFullscreen", std::addressof(vsFull) },
            { "PSSprite", std::addressof(psSprite) }, { "PSSolid", std::addressof(psSolid) },
            { "PSSpriteLum", std::addressof(psSpriteLum) }, { "PSSpriteDimmed", std::addressof(psSpriteDim) },
            { "PSSpriteDimmedMasked", std::addressof(psSpriteDimMasked) },
            { "PSSpriteDimmedMaskedPremul", std::addressof(psSpriteDimMaskedPre) }, { "PSGlow", std::addressof(psGlow) },
            { "PSDownThreshold", std::addressof(psDownT) }, { "PSDownsample", std::addressof(psDown) },
            { "PSBlur", std::addressof(psBlur) }, { "PSUpsample", std::addressof(psUp) },
            { "PSResolve", std::addressof(psResolve) }, { "PSBlit", std::addressof(psBlit) },
        };
        for (auto &f : fns) {
            auto *s = new ID3D11PixelShader();
            s->name = f.name;
            s->fn = [lib newFunctionWithName:[NSString stringWithUTF8String:f.name]];
            if (!s->fn) { log(L"gpu: missing shader %hs", f.name); s->Release(); return false; }
            f.out->Attach(s);
        }
        MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
        sd.minFilter = MTLSamplerMinMagFilterLinear;
        sd.magFilter = MTLSamplerMinMagFilterLinear;
        sd.mipFilter = MTLSamplerMipFilterLinear;
        sd.maxAnisotropy = 16;
        sd.sAddressMode = sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
        sd.lodMaxClamp = 2.0f;                    // see the Windows header: atlases bleed below level 2
        samp = [dev->dev newSamplerStateWithDescriptor:sd];
        log(L"gpu: %d shaders, 9 blend modes ready (Metal, %hs)", (int)ARRAYSIZE(fns), dev->dev.name.UTF8String);
        return samp != nil;
    }

    id<MTLRenderPipelineState> Pipe(ID3D11PixelShader *vs, ID3D11PixelShader *ps, Blend blend, MTLPixelFormat pf) {
        auto key = std::make_tuple((void *)vs, (void *)ps, (int)blend, (int)pf);
        auto it = pipes.find(key);
        if (it != pipes.end()) return it->second;
        MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = vs->fn;
        d.fragmentFunction = ps->fn;
        MTLRenderPipelineColorAttachmentDescriptor *c = d.colorAttachments[0];
        c.pixelFormat = pf;
        c.writeMask = MTLColorWriteMaskAll;
        typedef MTLBlendFactor F;
        struct S { F s, d, sa, da; bool on; };
        S b{};
        switch (blend) {
        case Blend::Normal:     b = { MTLBlendFactorSourceAlpha, MTLBlendFactorOneMinusSourceAlpha, MTLBlendFactorOne,  MTLBlendFactorOneMinusSourceAlpha, true }; break;
        case Blend::Premul:     b = { MTLBlendFactorOne, MTLBlendFactorOneMinusSourceAlpha, MTLBlendFactorOne,  MTLBlendFactorOneMinusSourceAlpha, true }; break;
        case Blend::Additive:   b = { MTLBlendFactorSourceAlpha, MTLBlendFactorOne, MTLBlendFactorOne, MTLBlendFactorOne, true }; break;
        case Blend::Multiply:   b = { MTLBlendFactorDestinationColor, MTLBlendFactorOneMinusSourceAlpha, MTLBlendFactorOne, MTLBlendFactorOneMinusSourceAlpha, true }; break;
        case Blend::Screen:     b = { MTLBlendFactorOne, MTLBlendFactorOneMinusSourceColor, MTLBlendFactorOne, MTLBlendFactorOneMinusSourceAlpha, true }; break;
        case Blend::Light:      b = { MTLBlendFactorOne, MTLBlendFactorOne, MTLBlendFactorZero, MTLBlendFactorOne, true }; break;
        case Blend::LightAlpha: b = { MTLBlendFactorSourceAlpha, MTLBlendFactorOne, MTLBlendFactorZero, MTLBlendFactorOne, true }; break;
        case Blend::Opaque:     b = { MTLBlendFactorOne, MTLBlendFactorZero, MTLBlendFactorOne, MTLBlendFactorZero, false }; break;
        case Blend::Erase:      b = { MTLBlendFactorZero, MTLBlendFactorOneMinusSourceAlpha, MTLBlendFactorZero, MTLBlendFactorOneMinusSourceAlpha, true }; break;
        }
        c.blendingEnabled = b.on ? YES : NO;
        c.rgbBlendOperation = c.alphaBlendOperation = MTLBlendOperationAdd;
        c.sourceRGBBlendFactor = b.s; c.destinationRGBBlendFactor = b.d;
        c.sourceAlphaBlendFactor = b.sa; c.destinationAlphaBlendFactor = b.da;
        NSError *err = nil;
        id<MTLRenderPipelineState> p = [dev->dev newRenderPipelineStateWithDescriptor:d error:&err];
        if (!p) NSLog(@"gpu: pipeline %s/%s failed: %@", vs->name.c_str(), ps->name.c_str(), err);
        pipes[key] = p;
        return p;
    }

    void SetCB(const CB &c) { cb = c; }
    void BindTarget(const Target &t) { dev->Bind(t.tex ? t.tex->tex : nil); }
    void Clear(const Target &t, float r = 0, float g = 0, float b = 0, float a = 0) {
        BindTarget(t);
        dev->ClearBound(r, g, b, a);
    }
    void UnbindSRVs() { mask = nil; }
    void UnbindSRV0() {}

    void UploadGeometry(const std::vector<Vertex> &v, const std::vector<uint16_t> &i) {
        if (v.empty() || i.empty()) return;
        // 32-bit indices: Metal wants the index offset 4-byte aligned, and a batch's
        // start in a 16-bit buffer is often not.
        std::vector<uint32_t> i32(i.begin(), i.end());
        vb = [dev->dev newBufferWithBytes:v.data() length:v.size() * sizeof(Vertex) options:MTLResourceStorageModeShared];
        ib = [dev->dev newBufferWithBytes:i32.data() length:i32.size() * 4 options:MTLResourceStorageModeShared];
    }

    void BeginSolid()                { curPS = psSolid.Get(); }
    void BeginSpritesLum()           { curPS = psSpriteLum.Get(); }
    void BeginSpritesMasked(ID3D11ShaderResourceView *m, bool premul = false) {
        curPS = premul ? psSpriteDimMaskedPre.Get() : psSpriteDimMasked.Get();
        mask = m ? m->tex : nil;
    }
    void BeginSprites(bool dimmed)   { curPS = dimmed ? psSpriteDim.Get() : psSprite.Get(); }

    void DrawIndexed(ID3D11ShaderResourceView *srv, Blend blend, UINT indexCount, UINT startIndex, UINT baseVertex) {
        if (!dev->bound || !vb || !ib || !curPS || indexCount == 0) return;
        id<MTLRenderPipelineState> p = Pipe(vsSprite.Get(), curPS, blend, dev->bound.pixelFormat);
        if (!p) return;
        id<MTLRenderCommandEncoder> e = dev->Enc();
        [e setRenderPipelineState:p];
        [e setCullMode:MTLCullModeNone];
        [e setVertexBuffer:vb offset:0 atIndex:0];
        [e setVertexBytes:&cb length:sizeof(CB) atIndex:1];
        [e setFragmentBytes:&cb length:sizeof(CB) atIndex:0];
        [e setFragmentTexture:(srv ? srv->tex : nil) atIndex:0];
        [e setFragmentTexture:mask atIndex:1];
        [e setFragmentSamplerState:samp atIndex:0];
        [e drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:indexCount indexType:MTLIndexTypeUInt32
                     indexBuffer:ib indexBufferOffset:(NSUInteger)startIndex * 4
                   instanceCount:1 baseVertex:(NSInteger)baseVertex baseInstance:0];
    }

    void FullscreenPass(ID3D11PixelShader *ps, ID3D11ShaderResourceView *a, ID3D11ShaderResourceView *b, Blend blend) {
        if (!dev->bound || !ps) return;
        id<MTLRenderPipelineState> p = Pipe(vsFull.Get(), ps, blend, dev->bound.pixelFormat);
        if (!p) return;
        id<MTLRenderCommandEncoder> e = dev->Enc();
        [e setRenderPipelineState:p];
        [e setCullMode:MTLCullModeNone];
        [e setFragmentBytes:&cb length:sizeof(CB) atIndex:0];
        [e setFragmentTexture:(a ? a->tex : nil) atIndex:0];
        [e setFragmentTexture:(b ? b->tex : nil) atIndex:1];
        [e setFragmentSamplerState:samp atIndex:0];
        [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    }
};

// The one readback: a small "A8" target (RGBA8 here), alpha channel only.
inline bool ReadbackAlpha(ID3D11Device *dev, ID3D11DeviceContext *, const Target &t, std::vector<float> &alphaOut) {
    if (!t.tex || !t.tex->tex) return false;
    dev->Flush(true);
    std::vector<uint8_t> px(size_t(t.w) * t.h * 4);
    [t.tex->tex getBytes:px.data() bytesPerRow:(NSUInteger)t.w * 4 fromRegion:MTLRegionMake2D(0, 0, t.w, t.h) mipmapLevel:0];
    alphaOut.assign(size_t(t.w) * t.h, 0.0f);
    for (size_t p = 0; p < alphaOut.size(); p++) alphaOut[p] = px[p * 4 + 3] / 255.0f;
    return true;
}

} // namespace gpu
