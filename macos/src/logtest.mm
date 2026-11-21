// shittim-logon / macos / src / logtest.mm
//
// host.h's rolling log without a screen: writes ~3.4 MB of lines into
// $HOME/Library/Logs/ShittimLogon/lockscreen.log and prints what is left. Run it with
// HOME pointing at a scratch directory:
//
//   HOME=/tmp/lt ~/shittim-mac/bin/logtest
//
// Expect lockscreen.log.1 just over 1.5 MiB, lockscreen.log under it, nothing else.

#include "host.h"

#include <sys/stat.h>

static long BytesOf(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 ? (long)st.st_size : -1;
}

int main() {
    host::OpenLog(host::DefaultLogPath());
    const std::string line(90, 'x');
    for (int i = 0; i < 34000; i++) host::Log(L"line %d %hs", i, line.c_str());
    const std::string p = host::LogPath();
    std::printf("log %ld bytes, .1 %ld bytes, .2 %ld (cap %ld)\n", BytesOf(p), BytesOf(p + ".1"), BytesOf(p + ".2"),
                host::kLogMaxBytes);
    return 0;
}
