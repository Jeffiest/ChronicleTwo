#include <libcdvd.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

#include "cdvd_file.hpp"

// The disc is the extracted tree again (cdvd_file.hpp). sceCdSearchFile resolves the names the
// game asks for - the DATA.* pair behind the ps2/src/dataread.cpp bodies, MOVIE/*.PSS for
// the movie player, a script's file for the CD_SEEK command - and hands back an lsn that names
// the host file. The streaming group takes that lsn back, so here the lsn is a handle over a
// file, never a disc address, and a streamed read is complete when it returns, as every host
// read is.
//
// sceCdRead is the plain sector read the drive once served. Every ps2/src/dataread.cpp body that
// called it is replaced by port/src/dataread.cpp (which reads through cdvd::ReadAt), so nothing on
// the host reaches it; it aborts rather than pretend a fetch that never happened. The CD streaming
// group below is the read path the port actually uses.

namespace {

// One file sceCdSearchFile has given out and the cursor the streaming calls keep over it.
struct StreamFile {
    cdvd::File    file;
    std::uint64_t pos      = 0;
    u_int         base_lsn = 0;
    bool          open     = false;
};

std::unordered_map<std::string, u_int> g_lsn_by_name;
std::unordered_map<u_int, cdvd::File>  g_file_by_lsn;
u_int                                  g_next_lsn = 0x10000;
StreamFile                             g_stream;

// The ISO name field, as the drive reported it: the base name only, upper case, without the
// ";1" version, at most the 15 characters the field holds.
void IsoName(const char *path, char *out_name, std::size_t size) {
    std::string name = path;

    if (std::size_t slash = name.find_last_of("/\\"); slash != std::string::npos) {
        name.erase(0, slash + 1);
    }

    if (std::size_t version = name.find(';'); version != std::string::npos) {
        name.erase(version);
    }

    if (name.size() >= size) {
        name.resize(size - 1);
    }

    for (char &ch : name) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }

    std::memcpy(out_name, name.c_str(), name.size() + 1);
}

} // namespace

int sceCdInit(int mode) {
    return 1;
}

int sceCdMmode(int media) {
    return 1;
}

int sceCdSearchFile(sceCdlFILE *file, const char *name) {
    if (file == NULL || name == NULL) {
        return 0;
    }

    std::string key = cdvd::Fold(name);

    if (key.empty()) {
        return 0;
    }

    u_int lsn;
    auto  known = g_lsn_by_name.find(key);

    if (known != g_lsn_by_name.end()) {
        lsn = known->second;
    } else {
        cdvd::File entry;

        if (!cdvd::Find(name, entry)) {
            return 0;
        }

        lsn = g_next_lsn++;
        g_lsn_by_name.emplace(key, lsn);
        g_file_by_lsn.emplace(lsn, entry);
    }

    const cdvd::File &entry = g_file_by_lsn.find(lsn)->second;

    *file = {};
    file->lsn = lsn;
    file->size = static_cast<u_int>(entry.size);
    IsoName(name, file->name, sizeof file->name);
    return 1;
}

int sceCdSeek(u_int lsn) {
    // The head has nothing to move: the streaming calls take the sector back through the search
    // table above, so the seek only has to succeed.
    return 1;
}

int sceCdRead(u_int lsn, u_int sectors, void *buffer, sceCdRMode *mode) {
    (void) lsn;
    (void) sectors;
    (void) buffer;
    (void) mode;
    PS2_UNIMPLEMENTED();
}

int sceCdSync(int mode) {
    return 0;
}

int sceCdGetError(void) {
    return 0;
}

int sceCdBreak(void) {
    return 0;
}

int sceCdDiskReady(int mode) {
    // SCECdComplete: the host file the drive was replaced with is always there.
    return 2;
}

int sceCdStInit(int sectors, int banks, void *buffer) {
    // The drive streamed through the EE buffer handed here; a host read needs no staging, so the
    // buffer is not kept.
    g_stream = {};
    return 1;
}

int sceCdStStart(unsigned int sector, sceCdRMode *mode) {
    auto found = g_file_by_lsn.find(sector);

    if (found == g_file_by_lsn.end()) {
        return 0;
    }

    g_stream.file = found->second;
    g_stream.pos = 0;
    g_stream.base_lsn = sector;
    g_stream.open = true;
    return 1;
}

int sceCdStSeekF(unsigned int sector) {
    if (!g_stream.open) {
        return 0;
    }

    // A stream seeks within its own file; positions before the start or past the end clamp.
    std::int64_t offset = (static_cast<std::int64_t>(sector) - static_cast<std::int64_t>(g_stream.base_lsn)) *
                          static_cast<std::int64_t>(cdvd::kSector);
    offset = std::clamp<std::int64_t>(offset, 0, static_cast<std::int64_t>(g_stream.file.size));
    g_stream.pos = static_cast<std::uint64_t>(offset);
    return 1;
}

int sceCdStStop(void) {
    g_stream = {};
    return 1;
}

int sceCdStRead(unsigned int sectors, void *buffer, unsigned int mode, unsigned int *error) {
    if (error != NULL) {
        *error = 0;
    }

    if (!g_stream.open || buffer == NULL) {
        return 0;
    }

    std::uint64_t remaining = g_stream.file.size - g_stream.pos;
    std::uint64_t want = static_cast<std::uint64_t>(sectors) * cdvd::kSector;

    if (remaining == 0 || want == 0) {
        return 0;
    }

    std::uint64_t take = std::min(want, remaining);

    if (!cdvd::ReadAt(g_stream.file, g_stream.pos, buffer, take)) {
        if (error != NULL) {
            *error = 1;
        }
        return 0;
    }

    g_stream.pos += take;

    // The drive handed back whole sectors, so a file that ends off the boundary has its tail
    // zeroed; report the sectors that held file bytes.
    std::uint64_t delivered = (take + cdvd::kSector - 1) / cdvd::kSector;
    std::uint64_t padded = delivered * cdvd::kSector;

    if (padded > take) {
        std::memset(static_cast<char *>(buffer) + take, 0, static_cast<std::size_t>(padded - take));
    }

    return static_cast<int>(delivered);
}
