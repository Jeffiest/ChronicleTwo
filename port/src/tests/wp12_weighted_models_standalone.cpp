#include <cassert>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif
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

void PortMgSkinMotionTree(mgCFrame *frame);

void CheckBindPose(mgCFrame *root, const char *name) {
    struct Vertices { mgCVisualMotionMDT *visual; std::vector<float> base; };
    std::vector<Vertices> models;
    for (int i = 0; i < root->frame_num; ++i) {
        mgCVisual *visual = root->frame_list[i]->visual;
        if (visual && visual->Iam() == MG_VISUAL_KIND_MOTION_MDT) {
            auto *motion = static_cast<mgCVisualMotionMDT *>(visual);
            models.push_back({motion, std::vector<float>(&motion->vertex[0][0],
                                                       &motion->vertex[0][0] + motion->vertex_num * 4)});
        }
    }
    for (int tick = 0; tick < 3; ++tick) {
        PortMgSkinMotionTree(root);
        for (const auto &model : models) {
            for (int i = 0; i < model.visual->vertex_num; ++i) for (int lane = 0; lane < 3; ++lane) {
                float expected = model.base[i * 4 + lane], actual = model.visual->vertex[i][lane];
                if (!std::isfinite(actual) || std::fabs(expected - actual) > 0.002f * (1 + std::fabs(expected))) {
                    std::fprintf(stderr, "bind pose mismatch: %s frame %d vertex %d lane %d expected %g actual %g\n",
                                 name, model.visual->frame_id, i, lane, expected, actual);
                    std::fprintf(stderr,
                                 "visual=%p vertex=%p weights=%p counts=%d/%d frame=%p base=%p basevertex=[%g,%g,%g,%g] current=[%g,%g,%g,%g]\n",
                                 (void *) model.visual, (void *) model.visual->vertex, (void *) model.visual->weight,
                                 model.visual->vertex_num, model.visual->weight_num, (void *) model.visual->frame,
                                 (void *) model.visual->base_matrix, model.base[i * 4], model.base[i * 4 + 1],
                                 model.base[i * 4 + 2], model.base[i * 4 + 3], model.visual->vertex[i][0],
                                 model.visual->vertex[i][1], model.visual->vertex[i][2], model.visual->vertex[i][3]);
                    if (i < model.visual->weight_num) {
                        const mgVertexWeight &weight = model.visual->weight[i];
                        std::fprintf(stderr, "influences: (%d,%g) (%d,%g) (%d,%g) (%d,%g); bones:",
                                     weight.matrix[0], weight.weight[0], weight.matrix[1], weight.weight[1],
                                     weight.matrix[2], weight.weight[2], weight.matrix[3], weight.weight[3]);
                        for (int slot = 0; slot < 32 && model.visual->bone[slot] >= 0; ++slot) {
                            std::fprintf(stderr, " %d", model.visual->bone[slot]);
                        }
                        std::fputc('\n', stderr);
                    }
                    std::exit(1);
                }
            }
        }
    }
}

int main(int argc, char **argv) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    assert(argc == 2);
    if (!std::filesystem::exists(std::filesystem::path(argv[1]) / "mainchr/c02fuku4.chr")) {
        std::puts("SKIP: private extracted PAL assets are unavailable");
        return 77;
    }
    PathsSetDataRoot(PathsFromUtf8(argv[1]));
    InitCDFile();
    int weighted_models = 0;
    for (const char *path : {"mainchr/c02fuku4.chr", "mainchr/c01fuku1.chr", "mainchr/ws050.chr",
                             "event/opening/p1/p1_starbre.chr"}) {
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
                    char all[] = "";
                    mgCreateVisualType visual_types[] = {{MG_VISUAL_CREATE_MOTION_MDT, all}, {MG_VISUAL_CREATE_END, nullptr}};
                    load.visual_type = visual_types;
                    mgCFrame *frames = mgLoadMDSFile(&load);
                    assert(frames && frames->frame_num == static_cast<int>(mds->object_num));
                    CheckBindPose(frames, name.c_str());
                    std::vector<u_long128> clone_storage(39616);
                    mgCMemory clones;
                    clones.stSetBuffer(clone_storage.data(), static_cast<int>(clone_storage.size()));
                    mgCFrame *copy = mgCopyFrame(frames, &clones, 1);
                    assert(copy && copy->frame_num == frames->frame_num);
                    float translated[4][4];
                    std::memcpy(translated, copy->trans_matrix, sizeof(translated));
                    translated[3][0] += 7; translated[3][1] += 9; translated[3][2] -= 3;
                    copy->SetTransMatrix(translated);
                    CheckBindPose(copy, name.c_str());
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
