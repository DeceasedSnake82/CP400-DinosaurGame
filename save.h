// High score persistence: a 9 byte record in a file on the calculator's storage
// (\\fls0\DinoHi.sav). The record format is pure (no SDK) so it can be tested on a PC.
#pragma once

#include <stdint.h>

namespace save {

constexpr int RECORD_SIZE = 9;

// 'D' 'N' 'O' version, score as 4 big-endian bytes, 1 check byte. Anything else is ignored.
inline void encode(uint32_t score, uint8_t out[RECORD_SIZE]) {
    out[0] = 'D'; out[1] = 'N'; out[2] = 'O'; out[3] = 1;
    out[4] = (uint8_t)(score >> 24); out[5] = (uint8_t)(score >> 16);
    out[6] = (uint8_t)(score >> 8);  out[7] = (uint8_t)score;
    uint8_t c = 0xA5;
    for (int i = 0; i < 8; ++i) c ^= out[i];
    out[8] = c;
}

inline bool decode(const uint8_t in[RECORD_SIZE], uint32_t& score) {
    if (in[0] != 'D' || in[1] != 'N' || in[2] != 'O' || in[3] != 1) return false;
    uint8_t c = 0xA5;
    for (int i = 0; i < 8; ++i) c ^= in[i];
    if (c != in[8]) return false;
    const uint32_t s = ((uint32_t)in[4] << 24) | ((uint32_t)in[5] << 16) | ((uint32_t)in[6] << 8) | in[7];
    if (s > 99999) return false;
    score = s;
    return true;
}

bool load(uint32_t& score);                 // false: no (valid) file
bool store(uint32_t score);                 // false: could not write it

}  // namespace save
