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
// The Hollyhock launcher's own file loader opens "\\fls0\\run.bin" in assembler, which is a SINGLE leading
// backslash; the SDK doc comment suggests a double one. A wrong guess gives FILE_ENOVOLUME (-7), so
// try the single one first and fall back to the double one.
const char* const PATHS[] = { "\\fls0\\DinoHi.sav", "\\\\fls0\\DinoHi.sav" };
constexpr int NUM_PATHS = (int)(sizeof PATHS / sizeof PATHS[0]);

// Open with the first (path, mode) combination that works. Returns an fd >= 0, or the first error.
int openAny(const int* modes, int numModes) {
    int firstErr = 0;
    for (int p = 0; p < NUM_PATHS; ++p) {
        for (int m = 0; m < numModes; ++m) {
            const int fd = File_Open(PATHS[p], modes[m]);
            if (fd >= 0) return fd;
            if (!firstErr) firstErr = fd;
            if (fd == FILE_ENOVOLUME || fd == FILE_ENOPATH) break;     // another mode will not help: next path
        }
    }
    return firstErr;
}
}  // namespace

int load(uint32_t& score) {
    static const int MODES[] = { FILE_OPEN_READ };
    const int fd = openAny(MODES, 1);
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
    const int fd = openAny(MODES, (int)(sizeof MODES / sizeof MODES[0]));
    if (fd < 0) return fd;
    const int n = File_Write(fd, rec, RECORD_SIZE);
    if (n == RECORD_SIZE) File_Flush(fd);
    const int c = File_Close(fd);
    if (n < 0) return n;
    if (n != RECORD_SIZE) return ERR_SHORT_IO;
    return c < 0 ? ERR_CLOSE : 0;
}

}  // namespace save
#endif
