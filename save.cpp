#include "save.h"

#ifdef DINO_HOST
// PC tests: a plain file (the harness deletes it before each run).
#include <stdio.h>
#ifndef DINO_SAVE_PATH
#define DINO_SAVE_PATH "out/dino.sav"
#endif

namespace save {

bool load(uint32_t& score) {
    FILE* f = fopen(DINO_SAVE_PATH, "rb");
    if (!f) return false;
    uint8_t rec[RECORD_SIZE];
    const size_t n = fread(rec, 1, sizeof rec, f);
    fclose(f);
    return n == sizeof rec && decode(rec, score);
}

bool store(uint32_t score) {
    uint8_t rec[RECORD_SIZE];
    encode(score, rec);
    FILE* f = fopen(DINO_SAVE_PATH, "wb");
    if (!f) return false;
    const bool ok = fwrite(rec, 1, sizeof rec, f) == sizeof rec;
    return fclose(f) == 0 && ok;
}

}  // namespace save

#else
#include <sdk/os/file.h>

namespace save {

namespace {
const char* const PATH = "\\\\fls0\\DinoHi.sav";          // = \\fls0\DinoHi.sav
}

bool load(uint32_t& score) {
    const int fd = File_Open(PATH, FILE_OPEN_READ);
    if (fd < 0) return false;
    uint8_t rec[RECORD_SIZE];
    const int n = File_Read(fd, rec, RECORD_SIZE);
    File_Close(fd);
    return n == RECORD_SIZE && decode(rec, score);
}

bool store(uint32_t score) {
    uint8_t rec[RECORD_SIZE];
    encode(score, rec);
    // The record always has the same length, so overwriting an existing file from the
    // start is enough whether or not the OS truncates on open.
    int fd = File_Open(PATH, FILE_OPEN_WRITE);
    if (fd < 0) fd = File_Open(PATH, FILE_OPEN_WRITE | FILE_OPEN_CREATE);
    if (fd < 0) return false;
    const int n = File_Write(fd, rec, RECORD_SIZE);
    const int c = File_Close(fd);
    return n == RECORD_SIZE && c >= 0;
}

}  // namespace save
#endif
