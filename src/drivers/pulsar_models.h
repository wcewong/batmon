#pragma once
// Model names of Pulsar's XS-1 generation mice (X2 / X2H / X2N / X3 / Xlite CrazyLight, the signature
// editions, ...). Their dongles are shared (3710:5406 calls itself "8K Dongle Gen.2"), so the model is
// only known from the customer / model id the mouse reports to ReadCIDMID (kysona.cpp).
// Table from Pulsar's web driver, https://bbb.pulsar.gg/cMouse/devicename.json (cid 87, entry = MID - 1);
// colourways and editions of one shape are folded into the shape's name. MIDs it lists as a placeholder
// ("Pulsar device") are left out. Verified on an X2N CrazyLight Mini (MID 26)
// through 3710:5406 and on its cable (3710:3510, which names itself "X2N CrazyLight").

inline const wchar_t* PulsarModelName(unsigned cid, unsigned mid) {
    struct Model { const wchar_t* name; unsigned char mids[24]; };
    static const Model kModels[] = {
        { L"Pulsar X2 CrazyLight Mini",
          { 1, 2, 3, 4, 5, 6, 9, 10, 12, 13, 14, 15, 16, 43, 44, 82, 85, 86, 95, 96, 130, 131, 132 } },
        { L"Pulsar X2 CrazyLight Medium", { 27, 70, 79, 80, 81, 83, 84, 87, 88, 89, 90, 109 } },
        { L"Pulsar X2 CrazyLight 5th Anniversary Edition", { 41 } },
        { L"Pulsar X2 CrazyLight WildScape Desert Edition", { 42 } },
        { L"Pulsar X2 CrazyLight 2 Mini", { 139, 146 } },
        { L"Pulsar X2 CrazyLight 2 Medium", { 140, 147 } },
        { L"Pulsar X2H CrazyLight Mini", { 23, 24, 97, 117 } },
        { L"Pulsar X2H CrazyLight Medium", { 29, 76, 98, 119 } },
        { L"Pulsar X2N CrazyLight Mini", { 25, 26, 105 } },
        { L"Pulsar X2N CrazyLight Medium", { 77, 78, 106 } },
        { L"Pulsar X2F CrazyLight", { 126 } },
        { L"Pulsar X3 CrazyLight Mini", { 19, 20, 102 } },
        { L"Pulsar X3 CrazyLight Medium", { 72, 73, 103 } },
        { L"Pulsar X3 LHD CrazyLight Mini", { 21, 22 } },
        { L"Pulsar X3 LHD CrazyLight Medium", { 74, 75, 104 } },
        { L"Pulsar Xlite CrazyLight Mini", { 17, 18, 116 } },
        { L"Pulsar Xlite CrazyLight Medium", { 34, 71, 118 } },
        { L"Pulsar X2 Mini By You XS-1", { 28 } },
        { L"Pulsar X2H Medium By You XS-1", { 30 } },
        { L"Pulsar X2A Medium By You XS-1", { 31 } },
        { L"Pulsar X2A Mini By You XS-1", { 32 } },
        { L"Pulsar Xlite Mini By You XS-1", { 33 } },
        { L"Pulsar Xlite Large By You XS-1", { 35 } },
        { L"Pulsar Lab. X2F", { 36, 67 } },
        { L"Pulsar eS FS-1", { 91, 127, 128, 129 } },
        { L"Pulsar Feinmann F01", { 101 } },
        { L"Pulsar TenZ", { 7, 8, 37, 40 } },
        { L"Pulsar TenZ 2.0", { 107, 123 } },
        { L"Pulsar TenZ Signature Edition 2.0", { 110, 122 } },
        { L"Pulsar ZywOo The Chosen Mouse Gen.2", { 68, 69, 100 } },
        { L"Pulsar ZywOo The Chosen Mouse Gen.3", { 133, 134, 135, 136 } },
        { L"Pulsar JV-X", { 38 } },
        { L"Pulsar Susanto-X", { 39 } },
        { L"Pulsar Susanto-X 2.0", { 112, 120, 124, 125 } },
        { L"Pulsar BUZZ-X", { 92 } },
        { L"Pulsar STA-X", { 93 } },
        { L"Pulsar CARPE-X", { 94 } },
        { L"Pulsar Jinggg-X", { 99 } },
        { L"Pulsar X VSPO!",
          { 11, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66 } },
    };
    if (cid != 87 || mid == 0) return nullptr;
    for (const Model& m : kModels)
        for (unsigned char id : m.mids)
            if (id == mid) return m.name;
    return nullptr;
}
