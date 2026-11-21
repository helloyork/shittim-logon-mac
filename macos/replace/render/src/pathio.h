// shittim-logon / macos / replace / render/src/pathio.h
//
// macOS stand-in for render/src/pathio.h. On macOS argv and every path are already
// UTF-8, so the wide conversions are plain re-encodings and Utf8Args passes argv
// through. Same names, same signatures, so the files that include it compile as is.

#pragma once

#include <string>
#include <codecvt>
#include <locale>

namespace sl {

inline std::wstring widenUtf8(const std::string &s) {
    std::wstring w;
    w.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp = 0xFFFD;
        int n = 1;
        if (c < 0x80) { cp = c; }
        else if ((c >> 5) == 6 && i + 1 < s.size()) { cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F); n = 2; }
        else if ((c >> 4) == 14 && i + 2 < s.size()) { cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); n = 3; }
        else if ((c >> 3) == 30 && i + 3 < s.size()) { cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F); n = 4; }
        w.push_back((wchar_t)cp);   // wchar_t is 32-bit on macOS
        i += n;
    }
    return w;
}

inline std::string narrowUtf8(const std::wstring &w) {
    std::string s;
    for (wchar_t wc : w) {
        uint32_t cp = (uint32_t)wc;
        if (cp < 0x80) s.push_back((char)cp);
        else if (cp < 0x800) { s.push_back((char)(0xC0 | (cp >> 6))); s.push_back((char)(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { s.push_back((char)(0xE0 | (cp >> 12))); s.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); s.push_back((char)(0x80 | (cp & 0x3F))); }
        else { s.push_back((char)(0xF0 | (cp >> 18))); s.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); s.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); s.push_back((char)(0x80 | (cp & 0x3F))); }
    }
    return s;
}

class Utf8Args {
public:
    Utf8Args(int argc, char **argv) : _argc(argc), _argv(argv) {}
    Utf8Args(const Utf8Args &) = delete;
    Utf8Args &operator=(const Utf8Args &) = delete;
    int argc() const { return _argc; }
    char **argv() const { return _argv; }
private:
    int _argc;
    char **_argv;
};

} // namespace sl
