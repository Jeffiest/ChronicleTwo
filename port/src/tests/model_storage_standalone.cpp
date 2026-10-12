#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <memory>
#include "cdvd_file.hpp"
#include "dataread.hpp"
#include "platform/paths.hpp"
#include "serialized_record_contracts.hpp"
#include "mg_dataset.hpp"
#include "visualmotion.hpp"

namespace {
struct alignas(16) Model {
    MDT_HEADER header{};
    sceVu0FVECTOR vertices[3]{{0, 0, 0, 1}, {1, 0, 0, 1}, {0, 1, 0, 1}};
    MDT_FACES faces{};
    struct { unsigned type, count, material; int indices[3]; } primitive{};
    Model() {
        std::memcpy(header.magic, "MDT", 4);
        header.header_size = sizeof(header);
        header.vertex_num = 3;
        header.vertex_ofs = offsetof(Model, vertices);
        header.faces_ofs = offsetof(Model, faces);
        header.faces_size = sizeof(faces) + sizeof(primitive);
        faces.prim_num = 1;
        primitive.type = static_cast<unsigned>(MG_PRIM_TRIANGLE) | MG_FACE_NO_NORMAL | MG_FACE_NO_TEXTURE;
        primitive.count = 3;
        primitive.indices[0] = 0; primitive.indices[1] = 1; primitive.indices[2] = 2;
    }
};
}

int main(int argc, char **argv) {
    static_assert(sizeof(MDT_HEADER) == 0x40);
    static_assert(sizeof(MDT_MATERIAL_) == 0x60);
    static_assert(sizeof(MDT_FACES) == 0x10);
    if (argc == 3 && std::strcmp(argv[1], "local-pal") == 0) {
        PathsSetDataRoot(PathsFromUtf8(argv[2]));
        InitCDFile();
        int total_models = 0;
        for (const char *name : {"mainchr/c01fuku1.chr", "mainchr/c02fuku4.chr", "mainchr/ws050.chr"}) {
            cdvd::File file;
            assert(cdvd::Find(name, file));
            std::vector<u_long128> bytes((file.size + 15) / 16);
            assert(cdvd::ReadAt(file, 0, bytes.data(), file.size));
            auto *begin = reinterpret_cast<unsigned char *>(bytes.data());
            std::vector<MDT_HEADER *> headers;
            for (size_t offset = 0; offset + sizeof(MDT_HEADER) <= file.size; offset += 4) {
                auto *header = reinterpret_cast<MDT_HEADER *>(begin + offset);
                if (std::memcmp(header->magic, "MDT", 3) != 0 || header->header_size != 64) continue;
                const auto available = file.size - offset;
                auto fits = [&](int at, int count, size_t stride) {
                    return at >= 0 && count >= 0 && serialized_record_contracts::RangeFits(
                        available, at, static_cast<uint64_t>(count) * stride);
                };
                assert(fits(header->vertex_ofs, header->vertex_num, 16));
                assert(fits(header->normal_ofs, header->normal_num, 16));
                assert(fits(header->colour_ofs, header->colour_num, 16));
                assert(fits(header->uv_ofs, header->uv_num, 16));
                assert(fits(header->material_ofs, header->material_num, sizeof(MDT_MATERIAL_)));
                assert(fits(header->faces_ofs, header->faces_size, 1));
                headers.push_back(header);
            }
            assert(!headers.empty());
            std::vector<u_long128> backing(39616), work_backing(16384);
            mgCMemory memory, work;
            memory.stSetBuffer(backing.data(), static_cast<int>(backing.size()));
            work.stSetBuffer(work_backing.data(), static_cast<int>(work_backing.size()));
            std::vector<std::unique_ptr<mgCVisualFixMDT>> fixed;
            for (auto *header : headers) {
                auto model = std::make_unique<mgCVisualFixMDT>();
                assert(model->DataAssignMDT(header, &memory, nullptr));
                fixed.push_back(std::move(model));
            }
            const int fixed_used = memory.stack_used;
            fixed.clear(); memory.stReset();
            std::vector<std::unique_ptr<mgCVisualMotionMDT>> motion;
            unsigned weights[8]{};
            mgCVMotionData data{}; data.weight_data = weights;
            for (auto *header : headers) {
                auto model = std::make_unique<mgCVisualMotionMDT>();
                assert(model->DataAssignMotionMDT(header, &data, &memory, &work, nullptr));
                motion.push_back(std::move(model));
            }
            std::printf("%s: %zu MDTs; fixed %d motion %d / 39616 blocks (geometry storage, synthetic empty weights)\n",
                        name, headers.size(), fixed_used, memory.stack_used);
            total_models += static_cast<int>(headers.size());
        }
        assert(total_models == 30);
        return 0;
    }
    Model source;
    std::vector<u_long128> storage(256), scratch_storage(64);
    mgCMemory memory, work;
    memory.stSetBuffer(storage.data(), argc > 1 ? 2 : static_cast<int>(storage.size()));
    work.stSetBuffer(scratch_storage.data(), static_cast<int>(scratch_storage.size()));
    if (argc > 1 && std::strcmp(argv[1], "insufficient") == 0) {
        mgCVisualFixMDT fixed;
        fixed.DataAssignMDT(&source.header, &memory, nullptr);
        return 1;
    }
    mgCVisualFixMDT fixed;
    assert(fixed.DataAssignMDT(&source.header, &memory, nullptr));
    assert(fixed.vertex != source.vertices && fixed.vertex[1][0] == 1);
    assert(fixed.face_group && fixed.face_group->face->index[2] == 2);
    assert(fixed.face_group->face->packet_tag == 0);
    assert(memory.stack_used == 11); // 3 vectors + 3 face + 1 indices + 4 group blocks.
    memory.stReset();
    mgCVisualMotionMDT motion;
    unsigned weights[8]{};
    mgCVMotionData motion_data{};
    motion_data.weight_data = weights;
    assert(motion.DataAssignMotionMDT(&source.header, &motion_data, &memory, &work, nullptr));
    assert(motion.face_group && motion.face_group->face->index[2] == 2);
    assert(motion.face_group->face->packet_tag == 0);
    assert(memory.stack_used == 10); // Scratch indices are retained separately; no VIF packet.
    work.stReset();
    std::memset(scratch_storage.data(), 0xA5, scratch_storage.size() * 16);
    assert(motion.face_group->face->index[2] == 2);
    source.vertices[1][0] = 99;
    assert(motion.vertex[1][0] == 1);
    for (int i = 0; i < 8; ++i) {
        memory.stReset();
        assert(motion.DataAssignMotionMDT(&source.header, &motion_data, &memory, &work, nullptr));
        assert(memory.stack_used == 10 && motion.face_group->face->index[2] == 2);
    }
    std::puts("fixed/motion native storage and scratch reload passed");
}
