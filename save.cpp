#include "save.h"

#ifdef DINO_HOST
// PC tests: a plain file (the harness deletes it before each run).
#include <stdio.h>
#ifndef DINO_SAVE_PATH
#define DINO_SAVE_PATH "out/dino.sav"
#endif

namespace save {

int load(uint32_t& score) {
    FILE* f = fopen(DINO_SAVE_PATH, "rb");
    if (!f) return -14;                                  // like FILE_ENOENT
    uint8_t rec[RECORD_SIZE];
    const size_t n = fread(rec, 1, sizeof rec, f);
    fclose(f);
    if (n != sizeof rec) return ERR_SHORT_IO;
    return decode(rec, score) ? 0 : ERR_BAD_RECORD;
}

int store(uint32_t score) {
    uint8_t rec[RECORD_SIZE];
    encode(score, rec);
    FILE* f = fopen(DINO_SAVE_PATH, "wb");
    if (!f) return -5;
    const bool ok = fwrite(rec, 1, sizeof rec, f) == sizeof rec;
    const bool closed = fclose(f) == 0;
    return !ok ? ERR_SHORT_IO : (closed ? 0 : ERR_CLOSE);
}

}  // namespace save

#else
#include <sdk/os/file.h>

namespace save {

namespace {
const char* const PATH = "\\\\fls0\\DinoHi.sav";          // = \\fls0\DinoHi.sav
}

int load(uint32_t& score) {
    const int fd = File_Open(PATH, FILE_OPEN_READ);
    if (fd < 0) return fd;
    uint8_t rec[RECORD_SIZE];
    const int n = File_Read(fd, rec, RECORD_SIZE);
    File_Close(fd);
    if (n < 0) return n;
    if (n != RECORD_SIZE) return ERR_SHORT_IO;
    return decode(rec, score) ? 0 : ERR_BAD_RECORD;
}

int store(uint32_t score) {
    uint8_t rec[RECORD_SIZE];
    encode(score, rec);
    // We do not know how the OS treats the open flags (does CREATE on an existing file fail? does
    // WRITE truncate?), so try the sensible combinations. The record always has the same length,
    // so overwriting from the start works whether or not the file gets truncated.
    static const int MODES[] = {
        FILE_OPEN_WRITE,                                   // file already exists
        FILE_OPEN_WRITE | FILE_OPEN_CREATE,                // first save
        FILE_OPEN_READ | FILE_OPEN_WRITE,
        FILE_OPEN_READ | FILE_OPEN_WRITE | FILE_OPEN_CREATE,
    };
    int fd = -1, firstErr = 0;
    for (int mode : MODES) {
        fd = File_Open(PATH, mode);
        if (fd >= 0) break;
        if (!firstErr) firstErr = fd;
    }
    if (fd < 0) return firstErr ? firstErr : fd;
    const int n = File_Write(fd, rec, RECORD_SIZE);
    if (n == RECORD_SIZE) File_Flush(fd);
    const int c = File_Close(fd);
    if (n < 0) return n;
    if (n != RECORD_SIZE) return ERR_SHORT_IO;
    return c < 0 ? ERR_CLOSE : 0;
}

}  // namespace save
#endif
