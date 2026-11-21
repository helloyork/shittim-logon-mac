// shittim-logon / macos / compat / windows.h
//
// The handful of Win32 types and calls that the shared headers (spine_layer.h and
// the render/src kernel) use, so they compile on macOS unchanged. Nothing here is a
// general Win32 emulation -- only what those files actually reach for.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>

typedef unsigned int UINT;
typedef uint32_t     UINT32;
typedef int32_t      LONG;
typedef uint32_t     DWORD;
typedef uint8_t      BYTE;
typedef int32_t      HRESULT;
#if defined(__OBJC__)
#include <objc/objc.h>       // BOOL is Objective-C's own there
#else
typedef int          BOOL;
#endif

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define S_OK          ((HRESULT)0)
#define E_FAIL        ((HRESULT)0x80004005)
#define E_NOINTERFACE ((HRESULT)0x80004002)
#define FAILED(hr)    (((HRESULT)(hr)) < 0)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)

#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))

struct RECT  { LONG left, top, right, bottom; };
struct POINT { LONG x, y; };

union LARGE_INTEGER {
    struct { uint32_t LowPart; int32_t HighPart; };
    int64_t QuadPart;
};

inline BOOL QueryPerformanceFrequency(LARGE_INTEGER *f) { f->QuadPart = 1000000000LL; return TRUE; }
inline BOOL QueryPerformanceCounter(LARGE_INTEGER *c) {
    c->QuadPart = (int64_t)clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    return TRUE;
}

#define CP_UTF8 65001
// UTF-8 to UTF-32 (wchar_t is 32-bit here). `n` = -1 means NUL-terminated and the
// terminator is counted, as on Windows.
inline int MultiByteToWideChar(UINT, DWORD, const char *s, int n, wchar_t *w, int cap) {
    if (!s) return 0;
    const size_t len = n < 0 ? std::strlen(s) + 1 : (size_t)n;
    int out = 0;
    for (size_t i = 0; i < len;) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp = 0xFFFD; int k = 1;
        if (c < 0x80) cp = c;
        else if ((c >> 5) == 6 && i + 1 < len) { cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F); k = 2; }
        else if ((c >> 4) == 14 && i + 2 < len) { cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); k = 3; }
        else if ((c >> 3) == 30 && i + 3 < len) { cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F); k = 4; }
        if (w && cap > 0) { if (out >= cap) break; w[out] = (wchar_t)cp; }
        out++;
        i += k;
    }
    return out;
}

// COM-style reference counting for the stand-in interfaces (see wrl/client.h).
struct IUnknown {
    std::atomic<int> refs{1};
    virtual ~IUnknown() {}
    unsigned long AddRef() { return (unsigned long)++refs; }
    unsigned long Release() { const int r = --refs; if (r == 0) delete this; return (unsigned long)r; }
};

// DXGI formats the shared code names.
enum DXGI_FORMAT {
    DXGI_FORMAT_UNKNOWN = 0,
    DXGI_FORMAT_R16G16B16A16_FLOAT = 10,
    DXGI_FORMAT_R8G8B8A8_UNORM = 28,
    DXGI_FORMAT_R16_UINT = 57,
    DXGI_FORMAT_A8_UNORM = 65,
    DXGI_FORMAT_B8G8R8A8_UNORM = 87,
};

// A surface D2D can wrap. On the Mac it is always a gpu::Target's texture.
struct IDXGISurface : IUnknown {};
