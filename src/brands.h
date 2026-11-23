#pragma once
// Brand look for a device: the icon in the maker's colour with a soft glow and a thin level bar
// under the percentage (like Razer Synapse / Logitech G HUB). Accent::None keeps the plain system look.
#include <string>
#include "devices.h"

enum class Accent {
    None,
    Razer, Logitech, SteelSeries, Corsair, HyperX, Rog, Zowie,
    PlayStation, Xbox, Galaxy, Apple, Jabra, Jbl, CoolerMaster, Steam, Redragon, Wobkey,
};

struct BrandColor { unsigned char r, g, b; };

struct BrandStyle {
    Accent accent;
    const wchar_t* name;  // "SteelSeries" (shown by --list)
    BrandColor dark;      // on a dark taskbar: the brand colour, brightened only where it is too dark to read
    BrandColor light;     // on a light taskbar: a deeper shade of the same hue (at least 3:1 contrast)
};

// Every brand with its colours, in enum order (Accent::None first).
const BrandStyle* AllBrands(int& count);
const BrandStyle& StyleOf(Accent a);
BrandColor AccentColor(Accent a, bool lightTaskbar);

// Picks the brand from the driver key ("steelseries:..."), then the USB / Bluetooth vendor id, then the name.
Accent BrandOf(const DeviceStatus& s);
