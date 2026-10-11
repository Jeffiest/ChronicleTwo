#include <cstdint>
#include <cstdio>

#include "../villagermngr.cpp"

namespace {

CVillagerPlace places_table[2];
GAME_PROGRESS_INFO progress_table[8];
bool progress_valid[8];

CVillagerPlaceInfo place_a;
CVillagerPlaceInfo place_b;
CVillagerPlaceInfo place_c;
CVillagerPlaceInfo place_d;

bool Check(bool condition, const char *message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

void Reset() {
    places_table[0] = CVillagerPlace();
    places_table[1] = CVillagerPlace();
    for (auto &entry : progress_table) entry = {};
    for (bool &valid : progress_valid) valid = false;
    place_a.map_no = place_b.map_no = place_c.map_no = place_d.map_no = 7;
}

void SetProgress(int id, int order) {
    progress_valid[id] = true;
    progress_table[id].order = order;
}

bool CheckOutput(const char *case_name, int got, int expected_count, const int *expected_ids,
                 CVillagerPlaceInfo *const *expected_places, const int *ids, CVillagerPlaceInfo *const *places) {
    bool ok = Check(got == expected_count, case_name);
    for (int i = 0; i < expected_count; ++i) {
        ok &= Check(ids[i] == expected_ids[i], "villager IDs retain table order");
        ok &= Check(places[i] == expected_places[i], "place pointers remain intact");
    }
    return ok;
}

struct GuardedOutputs {
    std::uintptr_t before;
    int ids[2];
    std::uintptr_t after_ids;
    std::uintptr_t before_places;
    CVillagerPlaceInfo *places[2];
    std::uintptr_t after_places;
};

bool CanariesIntact(const GuardedOutputs &out) {
    return out.before == 0x12345678u && out.after_ids == 0x23456789u &&
           out.before_places == 0x3456789Au && out.after_places == 0x456789ABu;
}

GuardedOutputs MakeOutputs() {
    GuardedOutputs out{};
    out.before = 0x12345678u;
    out.after_ids = 0x23456789u;
    out.before_places = 0x3456789Au;
    out.after_places = 0x456789ABu;
    return out;
}

bool PrimaryDayAndNight() {
    bool ok = true;
    for (int time = 0; time < 2; ++time) {
        Reset();
        SetProgress(4, 40);
        CVillagerPlace::ProgressInfo schedules[2]{};
        schedules[0].progress = 4;
        schedules[0].place[0][time] = &place_a;
        schedules[1].progress = 4;
        schedules[1].place[0][time] = &place_b;
        places_table[0].prog_num = 1;
        places_table[0].prog_info = &schedules[0];
        places_table[1].prog_num = 1;
        places_table[1].prog_info = &schedules[1];
        GuardedOutputs out = MakeOutputs();
        CVillagerMngr manager;
        const int got = manager.GetAppearVlgr(4, time, 7, out.ids, out.places);
        const int expected_ids[] = {0, 1};
        CVillagerPlaceInfo *expected_places[] = {&place_a, &place_b};
        ok &= CheckOutput("primary schedule returns two outputs", got, 2, expected_ids, expected_places,
                          out.ids, out.places);
        ok &= Check(CanariesIntact(out), "primary output writes preserve surrounding canaries");
    }
    return ok;
}

bool FallbackNight() {
    Reset();
    SetProgress(1, 10);
    SetProgress(2, 20);
    SetProgress(5, 50);
    CVillagerPlace::ProgressInfo schedules[2]{};
    schedules[0].progress = 1;
    schedules[0].place[0][1] = &place_c;
    schedules[0].place[1][1] = &place_d;
    schedules[1].progress = 2;
    places_table[0].prog_num = 2;
    places_table[0].prog_info = schedules;
    GuardedOutputs out = MakeOutputs();
    CVillagerMngr manager;
    const int got = manager.GetAppearVlgr(5, 1, 7, out.ids, out.places);
    const int expected_ids[] = {0, 0};
    CVillagerPlaceInfo *expected_places[] = {&place_c, &place_d};
    bool ok = CheckOutput("fallback returns two nighttime outputs", got, 2, expected_ids, expected_places,
                          out.ids, out.places);
    ok &= Check(CanariesIntact(out), "fallback output writes preserve surrounding canaries");
    return ok;
}

bool LowProgressNormalization() {
    Reset();
    SetProgress(1, 10);
    CVillagerPlace::ProgressInfo schedule{};
    schedule.progress = 1;
    schedule.place[0][0] = &place_a;
    places_table[0].prog_num = 1;
    places_table[0].prog_info = &schedule;
    GuardedOutputs out = MakeOutputs();
    CVillagerMngr manager;
    const int got = manager.GetAppearVlgr(1, 1, 7, out.ids, out.places);
    const int expected_ids[] = {0};
    CVillagerPlaceInfo *expected_places[] = {&place_a};
    bool ok = CheckOutput("low progress normalizes to progress 1 and daytime", got, 1, expected_ids,
                          expected_places, out.ids, out.places);
    ok &= Check(CanariesIntact(out), "normalized output writes preserve surrounding canaries");
    return ok;
}

bool InvalidMapAndMissingProgress() {
    Reset();
    SetProgress(1, 10);
    CVillagerPlace::ProgressInfo schedule{};
    schedule.progress = 1;
    schedule.place[0][0] = &place_a;
    places_table[0].prog_num = 1;
    places_table[0].prog_info = &schedule;
    GuardedOutputs out = MakeOutputs();
    CVillagerMngr manager;
    bool ok = Check(manager.GetAppearVlgr(1, 0, -1, out.ids, out.places) == 0,
                    "negative map returns no villagers");
    ok &= Check(CanariesIntact(out), "invalid map does not write output buffers");
    ok &= Check(manager.GetAppearVlgr(6, 0, 7, out.ids, out.places) == 0,
                 "missing current progress returns no villagers");
    ok &= Check(CanariesIntact(out), "missing progress does not write output buffers");
    return ok;
}

} // namespace

CVillagerPlace *GetVlgrPlaceTable(int *count) {
    *count = 2;
    return places_table;
}

GAME_PROGRESS_INFO *GetGameProgressInfo(int index) {
    if (index < 0 || index >= 8 || !progress_valid[index]) return NULL;
    return &progress_table[index];
}

int main() {
    bool ok = PrimaryDayAndNight();
    ok &= FallbackNight();
    ok &= LowProgressNormalization();
    ok &= InvalidMapAndMissingProgress();
    if (ok) std::puts("PASS: villager output pointers, schedule selection, normalization and guards");
    return ok ? 0 : 1;
}

CVillagerPlace::CVillagerPlace() : prog_num(0), prog_info(NULL) {}
CVillagerPlaceInfo::CVillagerPlaceInfo() : map_no(-1) {}
void CVillagerData::Initialize() {}
void CVillagerMngr::Initialize() {}
