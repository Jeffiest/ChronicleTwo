#pragma once

// Disk views deliberately use fixed-width types instead of engine runtime declarations. This
// keeps pointer-sized host fields out of disk contracts (notably DATA_HEADER's name union).
#include <cstddef>
#include <cstdint>

namespace serialized_record_contracts {

struct MdtHeader {
    char magic[4];
    int32_t fields[15];
};
struct MdtMaterial {
    float diffuse[4];
    float unknown10[4];
    float unknown20[4];
    float unknown30;
    char texture[32];
    int32_t unknown54;
    float extra[2];
};
struct MdtFaces {
    int32_t fields[4];
};
struct FacesIdPrefix {
    uint32_t type;
    uint32_t vertex_count;
    uint32_t material;
    int32_t first_index;
};
struct DataHd4Record {
    uint32_t name_offset;
    uint32_t size;
    uint32_t sector;
};
struct PackEntryDisk {
    char name[64];
    int32_t offset;
    int32_t size;
    int32_t next;
};
struct SavedataHeaderPrefix {
    uint8_t bytes[0x80];
};
struct Tim2PicturePrefix {
    uint32_t total_size;
    uint32_t clut_size;
    uint32_t image_size;
    uint16_t header_size;
    uint16_t clut_colors;
    uint8_t picture_format;
    uint8_t mipmap_count;
    uint8_t clut_type;
    uint8_t image_type;
    uint16_t width;
    uint16_t height;
    uint8_t gs_registers[0x28];
    uint32_t mip_sizes[4];
};

static_assert(sizeof(MdtHeader) == 0x40);
static_assert(offsetof(MdtHeader, fields) + 9 * 4 == 0x28);  // faces_ofs
static_assert(offsetof(MdtHeader, fields) + 13 * 4 == 0x38); // material_ofs
static_assert(sizeof(MdtMaterial) == 0x60);
static_assert(offsetof(MdtMaterial, texture) == 0x34);
static_assert(sizeof(MdtFaces) == 0x10);
static_assert(sizeof(FacesIdPrefix) == 0x10);
static_assert(sizeof(DataHd4Record) == 12);
static_assert(sizeof(PackEntryDisk) == 0x4C);
static_assert(offsetof(PackEntryDisk, offset) == 0x40);
static_assert(offsetof(PackEntryDisk, next) == 0x48);
static_assert(sizeof(SavedataHeaderPrefix) == 0x80);

static_assert(offsetof(Tim2PicturePrefix, total_size) == 0x00);
static_assert(offsetof(Tim2PicturePrefix, header_size) == 0x0C);
static_assert(offsetof(Tim2PicturePrefix, image_type) == 0x13);
static_assert(offsetof(Tim2PicturePrefix, width) == 0x14);
static_assert(offsetof(Tim2PicturePrefix, mip_sizes) == 0x40);

constexpr uint32_t ReadLe32(const uint8_t *bytes) {
    return static_cast<uint32_t>(bytes[0]) | static_cast<uint32_t>(bytes[1]) << 8 |
           static_cast<uint32_t>(bytes[2]) << 16 | static_cast<uint32_t>(bytes[3]) << 24;
}

constexpr bool RangeFits(size_t extent, size_t offset, size_t length) {
    return offset <= extent && length <= extent - offset;
}

constexpr bool RecordTableFits(size_t extent, uint32_t byte_length, size_t record_size) {
    return record_size != 0 && byte_length >= record_size && byte_length <= extent &&
           byte_length % record_size == 0;
}

} // namespace serialized_record_contracts
