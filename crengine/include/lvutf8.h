#ifndef LVUTF8_H_INCLUDED
#define LVUTF8_H_INCLUDED
#include <stddef.h>
#include <string.h>

// Reject overlong forms, surrogates, truncated sequences and values above U+10FFFF.
inline bool LVIsValidUtf8(const char *text, size_t size) {
    if (!text) return size == 0;
    for (size_t i = 0; i < size;) {
        unsigned char c = text[i++];
        if (c < 0x80) continue;
        unsigned value, minimum;
        size_t tail;
        if (c >= 0xc2 && c <= 0xdf) { value = c & 31; minimum = 0x80; tail = 1; }
        else if (c >= 0xe0 && c <= 0xef) { value = c & 15; minimum = 0x800; tail = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { value = c & 7; minimum = 0x10000; tail = 3; }
        else return false;
        if (tail > size - i) return false;
        while (tail--) {
            unsigned char next = text[i++];
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return false;
    }
    return true;
}

// Firmware keyboard buffers are sized in bytes, not Unicode characters.
inline void LVCopyUtf8(char *dest, size_t capacity, const char *source) {
    if (!dest || !capacity) return;
    dest[0] = 0;
    if (!source) return;
    size_t size = strlen(source);
    size_t count = size < capacity ? size : capacity - 1;
    if (count < size) {
        while (count && (static_cast<unsigned char>(source[count]) & 0xc0) == 0x80) --count;
    }
    if (!LVIsValidUtf8(source, count)) return;
    memcpy(dest, source, count);
    dest[count] = 0;
}
#endif
