#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sound_stream_data {

// Supports the disc root and dc2data's sound/ layout. Audio records are
// read individually so loading the index never loads the 969 MB data archive.
class Index {
public:
    bool LoadIndex(const std::filesystem::path& root, std::string& error);
    bool ReadPcm(std::string_view name, std::vector<std::int16_t>& samples,
                 std::uint32_t& sample_rate, std::uint16_t& channels,
                 std::string& error) const;

private:
    struct Record {
        std::string name;
        std::uint32_t size{};
        std::uint32_t sector{};
    };
    std::filesystem::path archive_;
    std::vector<Record> records_;
};

} // namespace sound_stream_data
