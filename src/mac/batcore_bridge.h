// Pure-C bridge over the C++ core's PollAllDevices(), so the Swift menu-bar app can call it through
// a bridging header without seeing any C++. Strings are heap-allocated UTF-8 and owned by the
// result; the caller must hand the whole result back to batmon_free().
#ifndef BATMON_BRIDGE_H
#define BATMON_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* key;         // stable identity across polls
    const char* name;        // human-readable device name
    const char* via;         // transport, e.g. "Bluetooth", "2.4 GHz receiver"
    const char* detail;      // extra breakdown, e.g. "L 80%, R 75%, case 40%"
    const char* level_text;  // coarse level when there is no percentage ("Low", "Full")
    const char* note;        // advice shown while there is no level
    int percent;             // 0..100, or -1 when unknown right now
    int kind;                // DeviceKind: 0 other,1 mouse,2 headset,3 keyboard,4 gamepad,5 phone,6 mic,7 speaker
    int charging;            // bool
    int full;                // bool: on power and fully charged
    int connected;           // bool: false means the receiver is present but the device is off
    uint16_t vid;            // USB/Bluetooth vendor id (picks the brand color)
} BatmonDevice;

typedef struct {
    BatmonDevice* items;
    int count;
} BatmonResult;

// Runs one poll of every supported device. Blocking (up to a couple of seconds); call off the main
// thread. Always returns a valid result (count may be 0); free it with batmon_free().
BatmonResult batmon_poll(void);

// Releases a result returned by batmon_poll().
void batmon_free(BatmonResult result);

// Releases background driver sessions before the app exits.
void batmon_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif  // BATMON_BRIDGE_H
