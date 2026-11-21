// shittim-logon / macos / replace / render/src/spine_io.h
//
// macOS stand-in for render/src/spine_io.h: paths are UTF-8 already, so the
// extension opens them with fopen. Keeps the original's checked reads.

#pragma once

#include <spine/Extension.h>
#include <spine/SpineString.h>
#include <cstdio>
#include "pathio.h"

namespace sl {

class WidePathExtension : public spine::DefaultSpineExtension {
public:
    virtual ~WidePathExtension() {}
protected:
    virtual char *_readFile(const spine::String &path, int *length) override {
        *length = 0;
        if (!path.buffer()) return nullptr;
        FILE *file = std::fopen(path.buffer(), "rb");
        if (!file) return nullptr;
        if (std::fseek(file, 0, SEEK_END) != 0) { std::fclose(file); return nullptr; }
        const long size = std::ftell(file);
        if (size < 0) { std::fclose(file); return nullptr; }
        std::rewind(file);
        char *data = spine::SpineExtension::alloc<char>((size_t)size, __FILE__, __LINE__);
        if (!data) { std::fclose(file); return nullptr; }
        const size_t got = std::fread(data, 1, (size_t)size, file);
        std::fclose(file);
        if (got != (size_t)size) { spine::SpineExtension::free(data, __FILE__, __LINE__); return nullptr; }
        *length = (int)size;
        return data;
    }
};

} // namespace sl
