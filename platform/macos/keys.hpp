#pragma once
struct Key {
    unsigned vk = 0, scan = 0, ext = 0;
};
inline Key mapKey(unsigned code) {
    // macOS hardware positions -> Windows set-1. Host layout determines text.
    static const unsigned mac[] = {0,  11, 8,  2,  14, 3, 5,  4,  34, 38, 40, 37, 46,
                                   45, 31, 35, 12, 15, 1, 17, 32, 9,  13, 7,  16, 6};
    static const unsigned scans[] = {30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
                                     49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44};
    for (unsigned i = 0; i < 26; ++i)
        if (code == mac[i]) return {'A' + i, scans[i], 0};
    struct Entry {
        unsigned mac, vk, scan, ext;
    };
    static const Entry other[] = {
        {18, '1', 2, 0},    {19, '2', 3, 0},    {20, '3', 4, 0},    {21, '4', 5, 0},    {23, '5', 6, 0},
        {22, '6', 7, 0},    {26, '7', 8, 0},    {28, '8', 9, 0},    {25, '9', 10, 0},   {29, '0', 11, 0},
        {27, 0xbd, 12, 0},  {24, 0xbb, 13, 0},  {51, 8, 14, 0},     {48, 9, 15, 0},     {33, 0xdb, 26, 0},
        {30, 0xdd, 27, 0},  {36, 13, 28, 0},    {41, 0xba, 39, 0},  {39, 0xde, 40, 0},  {50, 0xc0, 41, 0},
        {42, 0xdc, 43, 0},  {43, 0xbc, 51, 0},  {47, 0xbe, 52, 0},  {44, 0xbf, 53, 0},  {49, 32, 57, 0},
        {53, 27, 1, 0},     {57, 0x14, 58, 0},  {56, 0xa0, 42, 0},  {60, 0xa1, 54, 0},  {59, 0xa2, 29, 0},
        {62, 0xa3, 29, 1},  {58, 0xa4, 56, 0},  {61, 0xa5, 56, 1},  {55, 0x5b, 91, 1},  {54, 0x5c, 92, 1},
        {123, 0x25, 75, 1}, {124, 0x27, 77, 1}, {125, 0x28, 80, 1}, {126, 0x26, 72, 1}, {115, 0x24, 71, 1},
        {119, 0x23, 79, 1}, {116, 0x21, 73, 1}, {121, 0x22, 81, 1}, {117, 0x2e, 83, 1}, {114, 0x2d, 82, 1},
        {122, 0x70, 59, 0}, {120, 0x71, 60, 0}, {99, 0x72, 61, 0},  {118, 0x73, 62, 0}, {96, 0x74, 63, 0},
        {97, 0x75, 64, 0},  {98, 0x76, 65, 0},  {100, 0x77, 66, 0}, {101, 0x78, 67, 0}, {109, 0x79, 68, 0},
        {103, 0x7a, 87, 0}, {111, 0x7b, 88, 0}, {82, 0x60, 82, 0},  {83, 0x61, 79, 0},  {84, 0x62, 80, 0},
        {85, 0x63, 81, 0},  {86, 0x64, 75, 0},  {87, 0x65, 76, 0},  {88, 0x66, 77, 0},  {89, 0x67, 71, 0},
        {91, 0x68, 72, 0},  {92, 0x69, 73, 0},  {65, 0x6e, 83, 0},  {67, 0x6a, 55, 0},  {69, 0x6b, 78, 0},
        {75, 0x6f, 53, 1},  {78, 0x6d, 74, 0},  {76, 13, 28, 1}};
    for (auto e : other)
        if (e.mac == code) return {e.vk, e.scan, e.ext};
    return {};
}

// Host shortcut policy: Ctrl -> Command, Windows/Super -> Control,
// Alt -> Option. Keep the wire encoding unchanged for non-Mac hosts.
inline unsigned mapHostKey(unsigned scan, unsigned extended) {
    if (extended > 1) return 256;
    if (scan == 29) return extended ? 54 : 55;
    if (extended && scan == 91) return 59;
    if (extended && scan == 92) return 62;
    for (unsigned code = 0; code < 128; ++code) {
        const auto key = mapKey(code);
        if (key.vk && key.scan == scan && key.ext == extended) return code;
    }
    return 256;
}
