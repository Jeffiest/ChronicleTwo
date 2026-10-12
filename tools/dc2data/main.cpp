#include <cstdio>
#include <cstring>
#include <exception>

#include "dc2data.hpp"

namespace {

namespace dc = dc2data;
namespace fs = std::filesystem;

int Usage() {
    std::fputs("usage: dc2data extract <iso-or-directory> <data-directory>\n"
               "       dc2data list <iso-or-directory>\n"
               "       dc2data check <data-directory>\n",
               stderr);
    return 2;
}

void Describe(const dc::Archive &archive) {
    dc::Log(stdout, "source: {} ({})\n", archive.source.string(), archive.image ? "ISO 9660 image" : "directory");
    dc::Log(stdout, "DATA.DAT: {} bytes, DATA.HD4: {} bytes, SOUND.DAT: {} bytes\n", archive.dat.size,
            archive.hd4.size, archive.sound_dat.size);
}

int List(const fs::path &source) {
    dc::Archive                     archive = dc::OpenArchive(source);
    std::vector<dc::Record>         records = dc::ReadIndex(archive);
    std::size_t                     duplicates = 0;
    std::vector<const dc::Record *> reachable = dc::Reachable(records, stderr, &duplicates);
    std::uint64_t                   bytes = 0;
    for (const dc::Record &record : records) {
        dc::Log(stdout, "{}\t{}\t{}\n", record.name.empty() ? std::string("<no name>") : record.name, record.size,
                record.sector);
    }
    for (const dc::Record *record : reachable) {
        bytes += record->size;
    }
    dc::Log(stderr, "{} entries, {} files, {} duplicates, {} bytes\n", records.size(), reachable.size(),
            duplicates, bytes);
    return 0;
}

int Extract(const fs::path &source, const fs::path &out) {
    dc::Archive archive = dc::OpenArchive(source);
    Describe(archive);
    dc::Summary summary = dc::Extract(archive, out, stderr);
    dc::Log(stdout, "index: {} records, {} files, {} duplicates, {} bytes\n", summary.records, summary.files,
            summary.duplicates, summary.bytes);
    if (summary.written == 0 && summary.removed == 0 && !summary.manifest_changed) {
        dc::Log(stdout, "nothing changed: {} files already in {}\n", summary.kept, out.string());
    } else {
        dc::Log(stdout, "wrote {} files ({} bytes), kept {} already in place, removed {} stale, manifest {}\n",
                summary.written, summary.written_bytes, summary.kept, summary.removed,
                summary.manifest_changed ? "updated" : "unchanged");
    }
    dc::Log(stdout, "verified {} files in {}\n", summary.verified, out.string());
    return 0;
}

int Check(const fs::path &out) {
    dc::CheckReport report = dc::CheckManifest(out);
    if (!report.problems.empty()) {
        for (const std::string &problem : report.problems) {
            dc::Log(stderr, "{}\n", problem);
        }
        return 1;
    }
    dc::Log(stdout, "checked {} files\n", report.checked);
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::strcmp(argv[1], "extract") == 0) {
            return Extract(argv[2], argv[3]);
        }
        if (argc == 3 && std::strcmp(argv[1], "list") == 0) {
            return List(argv[2]);
        }
        if (argc == 3 && std::strcmp(argv[1], "check") == 0) {
            return Check(argv[2]);
        }
        return Usage();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "dc2data: %s\n", error.what());
        return 1;
    }
}
