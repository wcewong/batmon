// C bridge implementation: converts the C++ DeviceStatus vector into a flat C array of UTF-8 PODs.
#include "batcore_bridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../devices.h"
#include "../platform_compat.h"

static char* Dup(const std::wstring& w) {
    std::string s = WideToUtf8(w);
    char* p = (char*)malloc(s.size() + 1);
    memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

extern "C" BatmonResult batmon_poll(void) {
    std::vector<DeviceStatus> devs = PollAllDevices();
    if (getenv("BATMON_DEBUG")) {
        fprintf(stderr, "[bridge] %zu device(s):", devs.size());
        for (const auto& d : devs) fprintf(stderr, " {%ls %d%%}", d.name.c_str(), d.percent);
        fprintf(stderr, "\n");
    }
    BatmonResult r;
    r.count = (int)devs.size();
    r.items = r.count ? (BatmonDevice*)calloc(r.count, sizeof(BatmonDevice)) : nullptr;
    for (int i = 0; i < r.count; i++) {
        const DeviceStatus& d = devs[i];
        BatmonDevice& b = r.items[i];
        b.key = Dup(d.key);
        b.name = Dup(d.name);
        b.via = Dup(d.via);
        b.detail = Dup(d.detail);
        b.level_text = Dup(d.levelText);
        b.note = Dup(d.note);
        b.percent = d.percent;
        b.kind = (int)d.kind;
        b.charging = d.charging ? 1 : 0;
        b.full = d.full ? 1 : 0;
        b.connected = d.connected ? 1 : 0;
        b.vid = d.vid;
    }
    return r;
}

extern "C" void batmon_free(BatmonResult r) {
    for (int i = 0; i < r.count; i++) {
        BatmonDevice& b = r.items[i];
        free((void*)b.key);
        free((void*)b.name);
        free((void*)b.via);
        free((void*)b.detail);
        free((void*)b.level_text);
        free((void*)b.note);
    }
    free(r.items);
}

extern "C" void batmon_shutdown(void) { ShutdownDevices(); }
