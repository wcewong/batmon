#pragma once
// One BLE advertisement watcher (WinRT BluetoothLEAdvertisementWatcher, passive scan) shared by the
// drivers that read battery levels from broadcasts. It only listens; nothing is ever sent.
// Received messages are handed to the parsers below on a WinRT thread pool thread, so they must
// lock whatever they store.
#include <windows.h>

// Starts the watcher, or restarts it after the radio was switched off. Returns true when it was just
// started, so the caller can give devices a moment (they broadcast about every 1-2 s) to be heard.
bool EnsureBleWatcher();

// Apple manufacturer data (company 0x004C), see airpods.cpp.
void AirPodsOnAdvert(UINT64 addr, int rssi, const BYTE* data, UINT32 len);
// Google Fast Pair service data (16-bit UUID 0xFE2C, bytes after the UUID), see fastpair.cpp.
void FastPairOnAdvert(UINT64 addr, const BYTE* data, UINT32 len);

// Battery split from the latest Fast Pair broadcast (percent, -1 unknown).
struct FastPairBattery {
    int left = -1, right = -1, box = -1;
    bool leftChg = false, rightChg = false, boxChg = false;
    ULONGLONG tick = 0;
};
// Latest broadcast of the device with this Bluetooth address, if heard in the last 5 minutes.
bool FastPairLookup(UINT64 addr, FastPairBattery& out);

// Asks for an active scan from now on (scan responses carry device names). Takes effect on the next
// EnsureBleWatcher calls; passive otherwise. Call from the poll thread, like EnsureBleWatcher.
void BleWantActiveScan();
// An advertisement whose name contains "ARMOR-X" (BigBigWon Armor X Pro), see armorx.cpp.
void ArmorXOnAdvert(UINT64 addr);

// The standard LE Battery Level (0x180F / 0x2A19) at this address, without pairing; -1 on failure.
// Blocking for seconds: call from a worker thread with WinRT initialised (marshall.cpp). The address
// must have been heard by the watcher recently. `settle` reads twice, 2.5 s apart (a device that
// reports a placeholder right after connecting).
int ReadLeBatteryLevel(UINT64 address, bool randomAddress, bool settle);
