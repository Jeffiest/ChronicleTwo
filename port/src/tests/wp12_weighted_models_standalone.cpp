#include <cassert>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "cdvd_file.hpp"
#include "dataread.hpp"
#include "mg_dataset.hpp"
#include "mg_frame.hpp"
#include "platform/paths.hpp"
#include "visualmotion.hpp"

int main(int argc, char **argv) {
    assert(argc == 2);
    if (!std::filesystem::exists(std::filesystem::path(argv[1]) / "mainchr/c02fuku4.chr")) {
        std::puts("SKIP: private extracted PAL assets are unavailable");
        return 77;
    }
    PathsSetDataRoot(PathsFromUtf8(argv[1]));
    InitCDFile();
    int weighted_models = 0;
    for (const char *path : {"mainchr/c02fuku4.chr", "mainchr/c01fuku1.chr", "mainchr/ws050.chr"}) {
        cdvd::File file;
        assert(cdvd::Find(path, file));
        std::vector<u_long128> bytes((file.size + 15) / 16);
        assert(cdvd::ReadAt(file, 0, bytes.data(), file.size));
        auto *pack = reinterpret_cast<u_int *>(bytes.data());
        auto *entry = reinterpret_cast<PACK_ENTRY *>(pack);
        while (entry->name[0] != 0) {
            const std::string name(entry->name);
            if (name.ends_with(".mds")) {
                std::string model_name = name, weight_name = name.substr(0, name.size() - 4) + ".wgt";
                auto *mds = reinterpret_cast<MDS_HEADER *>(GetPackFile(pack, model_name.data(), nullptr));
                auto *weights = static_cast<u_int *>(GetPackFile(pack, weight_name.data(), nullptr));
                if (weights != nullptr) {
                    std::vector<u_long128> skeleton_storage(39616), storage(39618), scratch(16386);
                    mgCMemory skeleton, memory, work;
                    skeleton.stSetBuffer(skeleton_storage.data(), 39616);
                    memory.stSetBuffer(storage.data() + 1, 39616);
                    work.stSetBuffer(scratch.data() + 1, 16384);
                    std::memset(storage.data(), 0xCC, 16);
                    std::memset(storage.data() + 39617, 0xCC, 16);
                    mgLoadData load{};
                    load.mds = mds; load.weight = weights; load.memory = &skeleton; load.work_memory = &work;
                    mgCFrame *frames = mgLoadMDSFile(&load);
                    assert(frames && frames->frame_num == static_cast<int>(mds->object_num));
                    auto *object = reinterpret_cast<MDTOBJ_HEADER *>(reinterpret_cast<char *>(mds) + mds->object_ofs);
                    for (unsigned frame_id = 0; frame_id < mds->object_num; ++frame_id) {
                        if (object->mdt_ofs != 0) {
                            auto *header = reinterpret_cast<MDT_HEADER *>(reinterpret_cast<char *>(mds) + object->mdt_ofs);
                            if (header->vertex_num > 0) {
                                mgCVMotionData data{};
                                data.weight_data = weights; data.frame_id = frame_id;
                                data.frame = frames->frame_list; data.base_matrix = frames->init_matrix;
                                mgCVisualMotionMDT model;
                                int expected_blocks = -1;
                                for (int reload = 0; reload < 8; ++reload) {
                                    memory.stReset(); work.stReset();
                                    assert(model.DataAssignMotionMDT(header, &data, &memory, &work, nullptr));
                                    assert(model.weight && model.weight_num == header->vertex_num);
                                    bool influenced = false;
                                    for (int i = 0; i < model.weight_num; ++i)
                                        for (float w : model.weight[i].weight) influenced |= w != 0.0f;
                                    assert(influenced);
                                    std::vector<unsigned char> before(model.weight_num * sizeof(mgVertexWeight));
                                    std::memcpy(before.data(), model.weight, before.size());
                                    work.stReset();
                                    std::memset(scratch.data() + 1, 0xA5, 16384 * 16);
                                    assert(std::memcmp(before.data(), model.weight, before.size()) == 0);
                                    if (expected_blocks < 0) expected_blocks = memory.stack_used;
                                    assert(memory.stack_used == expected_blocks);
                                    for (int i = 0; i < 16; ++i) {
                                        assert(reinterpret_cast<unsigned char *>(storage.data())[i] == 0xCC);
                                        assert(reinterpret_cast<unsigned char *>(storage.data() + 39617)[i] == 0xCC);
                                    }
                                }
                                ++weighted_models;
                            }
                        }
                        object = reinterpret_cast<MDTOBJ_HEADER *>(reinterpret_cast<char *>(object) + object->size);
                    }
                    std::printf("real pack model/weights/skeleton: %s / %s\n", path, name.c_str());
                }
            }
            assert(entry->next > 0);
            entry = reinterpret_cast<PACK_ENTRY *>(reinterpret_cast<char *>(entry) + entry->next);
        }
    }
    assert(weighted_models > 0);
    std::printf("%d actual weighted MDTs survive scratch reuse and eight reloads\n", weighted_models);
}
