// shittim-logon / macos / compat / wrl / client.h
//
// Microsoft::WRL::ComPtr, as much of it as the shared headers use: Get, Reset,
// operator& (releases, then hands out the slot), As, and bool tests.

#pragma once

#include <cstddef>
#include <utility>
#include "../windows.h"

namespace Microsoft { namespace WRL {

template <class T> class ComPtr;

// What `&ptr` yields, as in WRL: usable as the out slot of a factory (`T**`, after
// releasing what was there) and as `ComPtr<T>*` for `As(&other)`.
template <class T> struct ComPtrRef {
    ComPtr<T> *c;
    operator T **() { return c->ReleaseAndGetAddressOf(); }
    operator ComPtr<T> *() { return c; }
};

template <class T> class ComPtr {
public:
    ComPtr() = default;
    ComPtr(std::nullptr_t) {}
    explicit ComPtr(T *x) : p(x) { if (p) p->AddRef(); }
    ComPtr(const ComPtr &o) : p(o.p) { if (p) p->AddRef(); }
    ComPtr(ComPtr &&o) noexcept : p(o.p) { o.p = nullptr; }
    ~ComPtr() { Reset(); }

    ComPtr &operator=(const ComPtr &o) { if (this != &o) { Reset(); p = o.p; if (p) p->AddRef(); } return *this; }
    ComPtr &operator=(ComPtr &&o) noexcept { if (this != &o) { Reset(); p = o.p; o.p = nullptr; } return *this; }
    ComPtr &operator=(std::nullptr_t) { Reset(); return *this; }

    T *Get() const { return p; }
    T *operator->() const { return p; }
    ComPtrRef<T> operator&() { return ComPtrRef<T>{ this }; }
    T **GetAddressOf() { return &p; }
    T *const *GetAddressOf() const { return &p; }
    T **ReleaseAndGetAddressOf() { Reset(); return &p; }
    void Reset() { if (p) { T *q = p; p = nullptr; q->Release(); } }
    explicit operator bool() const { return p != nullptr; }

    // Adopts a pointer that already carries one reference.
    void Attach(T *x) { Reset(); p = x; }

    template <class U> HRESULT As(ComPtr<U> *out) const {
        U *u = dynamic_cast<U *>(p);
        if (!u) return E_NOINTERFACE;
        *out = ComPtr<U>(u);
        return S_OK;
    }
    template <class U> HRESULT As(ComPtrRef<U> r) const { return As(static_cast<ComPtr<U> *>(r)); }

private:
    T *p = nullptr;
};

}} // namespace Microsoft::WRL
