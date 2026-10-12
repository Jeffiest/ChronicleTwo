#include "sound_stream_data.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <unordered_map>

namespace sound_stream_data {
namespace {
constexpr std::uint64_t kSectorSize = 2048;
constexpr std::uint32_t kMaxIndexSize = 16 * 1024 * 1024;
constexpr std::uint32_t kMaxEntrySize = 64 * 1024 * 1024;

std::uint32_t U32(const unsigned char* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
std::uint16_t U16(const unsigned char* p) {
    return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8);
}
std::string Fold(std::string_view s) {
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return r;
}
bool ReadAt(std::ifstream& f, std::uint64_t off, unsigned char* dst, std::size_t n) {
    if (off > std::uint64_t(std::numeric_limits<std::streamoff>::max())) return false;
    f.clear(); f.seekg(static_cast<std::streamoff>(off));
    return bool(f) && bool(f.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n)));
}
bool FourCC(const unsigned char* p, const char* s) {
    return p[0] == s[0] && p[1] == s[1] && p[2] == s[2] && p[3] == s[3];
}
}

bool Index::LoadIndex(const std::filesystem::path& root, std::string& error) {
    error.clear();
    std::error_code ec;
    auto path = root / "SOUND.HD3";
    auto archive = root / "SOUND.DAT";
    if (!std::filesystem::exists(path, ec)) {
        path = root / "sound" / "sound.hd3";
        archive = root / "sound" / "sound.dat";
    }
    ec.clear();
    auto length = std::filesystem::file_size(path, ec);
    if (ec || length < 16 || length > kMaxIndexSize) { error = "SOUND.HD3 missing or invalid size"; return false; }
    std::ifstream f(path, std::ios::binary);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    if (!f || !f.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        error = "cannot read SOUND.HD3"; return false;
    }
    const std::uint32_t name_start = U32(bytes.data());
    if (name_start < 16 || (name_start & 15) || name_start > bytes.size()) { error = "invalid SOUND.HD3 record table offset"; return false; }
    const std::size_t count = name_start / 16;
    if (count > bytes.size() / 16) { error = "truncated SOUND.HD3 record table"; return false; }
    std::vector<Record> parsed;
    parsed.reserve(count - 1);
    for (std::size_t i = 0; i < count; ++i) {
        const auto* rec = bytes.data() + i * 16;
        const auto no = U32(rec), size = U32(rec + 4), sector = U32(rec + 8), sectors = U32(rec + 12);
        if (i + 1 == count) {
            if (no < name_start || no >= bytes.size() || bytes[no] != 0 || size || sector || sectors) {
                error = "invalid SOUND.HD3 terminator"; return false;
            }
            continue;
        }
        if (size == 0 || size > kMaxEntrySize || no < name_start || no >= bytes.size() || sectors == 0 ||
            sectors != (std::uint64_t(size) + kSectorSize - 1) / kSectorSize) {
            error = "invalid SOUND.HD3 record bounds"; return false;
        }
        auto end = std::find(bytes.begin() + no, bytes.end(), 0);
        if (end == bytes.end() || end == bytes.begin() + no) { error = "unterminated SOUND.HD3 name"; return false; }
        std::string name(reinterpret_cast<const char*>(bytes.data() + no), std::size_t(end - (bytes.begin() + no)));
        if (name.size() > 63 || name.find_first_of("/:\\") != std::string::npos) { error = "invalid SOUND.HD3 name"; return false; }
        parsed.push_back({Fold(name), size, sector});
    }
    archive_ = std::move(archive);
    records_ = std::move(parsed);
    return true;
}

bool Index::ReadPcm(std::string_view name, std::vector<std::int16_t>& samples,
                    std::uint32_t& sample_rate, std::uint16_t& channels, std::string& error) const {
    error.clear(); samples.clear(); sample_rate = 0; channels = 0;
    if (records_.empty()) { error = "sound index is not loaded"; return false; }
    if (name.empty() || name.find_first_of("/:\\") != std::string_view::npos) { error = "invalid sound name"; return false; }
    const auto key = Fold(name);
    const Record* found = nullptr;
    for (const auto& r : records_) if (r.name == key) { found = &r; break; } // duplicate names: first wins
    if (!found) { error = "sound not found"; return false; }
    std::error_code ec;
    std::ifstream f(archive_, std::ios::binary);
    const auto archive_size = std::filesystem::file_size(archive_, ec);
    const std::uint64_t offset = std::uint64_t(found->sector) * kSectorSize;
    if (!f || ec || offset > archive_size || found->size > archive_size - offset) { error = "SOUND.DAT record outside archive"; return false; }
    std::array<unsigned char, 12> head{};
    if (!ReadAt(f, offset, head.data(), head.size()) || !FourCC(head.data(), "RIFF") || !FourCC(head.data()+8, "WAVE")) {
        error = "invalid RIFF/WAVE header"; return false;
    }
    const std::uint64_t riff_end = std::uint64_t(U32(head.data()+4)) + 8;
    if (riff_end < 12 || riff_end > found->size) { error = "RIFF size exceeds indexed entry"; return false; }
    bool have_fmt = false, have_data = false;
    std::uint16_t fmt_tag = 0, bits = 0, block_align = 0;
    std::uint32_t byte_rate = 0;
    std::uint64_t data_off = 0, data_len = 0;
    for (std::uint64_t pos = 12; pos < riff_end;) {
        if (riff_end - pos < 8) { error = "truncated RIFF chunk header"; return false; }
        std::array<unsigned char, 8> ch{};
        if (!ReadAt(f, offset + pos, ch.data(), ch.size())) { error = "truncated RIFF chunk header"; return false; }
        const std::uint32_t len = U32(ch.data()+4);
        const std::uint64_t body = pos + 8;
        if (len > riff_end - body) { error = "RIFF chunk exceeds declared bounds"; return false; }
        if (FourCC(ch.data(), "fmt ")) {
            if (len < 16) { error = "short WAVE fmt chunk"; return false; }
            std::array<unsigned char, 16> fmt{};
            if (!ReadAt(f, offset + body, fmt.data(), fmt.size())) { error = "truncated WAVE fmt chunk"; return false; }
            fmt_tag = U16(fmt.data()); channels = U16(fmt.data()+2); sample_rate = U32(fmt.data()+4);
            byte_rate = U32(fmt.data()+8); block_align = U16(fmt.data()+12);
            bits = U16(fmt.data()+14); have_fmt = true;
        } else if (FourCC(ch.data(), "data") && !have_data) { data_off = body; data_len = len; have_data = true; }
        const std::uint64_t step = std::uint64_t(len) + (len & 1u);
        if (step > riff_end - body) { error = "RIFF chunk padding exceeds declared bounds"; return false; }
        pos = body + step;
    }
    if (!have_fmt || !have_data || fmt_tag != 1 || (channels != 1 && channels != 2) || !sample_rate || bits != 16 ||
        block_align != channels * 2 || byte_rate != std::uint64_t(sample_rate) * block_align ||
        data_len % block_align != 0 || data_len / 2 > std::numeric_limits<std::size_t>::max()) {
        error = "unsupported or incomplete PCM WAVE"; return false;
    }
    samples.resize(static_cast<std::size_t>(data_len / 2));
    if (data_len && !ReadAt(f, offset + data_off, reinterpret_cast<unsigned char*>(samples.data()), static_cast<std::size_t>(data_len))) {
        samples.clear(); error = "truncated WAVE data"; return false;
    }
    return true;
}
}
