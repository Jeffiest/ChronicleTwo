#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "event.hpp"
#include "mg_memory.hpp"
#include "moonmod/runtime.hpp"
#include "native_script_program.hpp"
#include "platform/arena.hpp"
#include "runscript.hpp"
#include "stubs/event.hpp"

namespace {
namespace fs = std::filesystem;

struct EventProgram {
    RS_PROG_HEADER header{};
    RS_PROGDATA    entry{.no = 100, .func = offsetof(EventProgram, function)};
    funcdata       function{};
    vmcode_t       code{.op = RS_OP_END, .arg1 = 0, .arg2 = 0};

    EventProgram() {
        std::memcpy(header.magic, "SB2", 3);
        header.main = offsetof(EventProgram, function);
        header.code = offsetof(EventProgram, code);
        header.prog = offsetof(EventProgram, entry);
        header.prog_num = 1;
    }
};

struct TempRoot {
    fs::path path = fs::temp_directory_path() /
                    ("moonmod-event-hook-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TempRoot() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

void Write(const fs::path &path, const std::string &content) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
    EXPECT_TRUE(output.good()) << path.string();
}
} // namespace

TEST(EventRuntimeMemory, ScriptBytesCanUseTheRetailBufferBudget) {
    static char key;
    auto       *storage = reinterpret_cast<u_long128 *>(ArenaBlock(&key, 20000 * sizeof(u_long128)));
    mgCMemory   memory;
    memory.stSetBuffer(storage, 20000);
    // The d05 chapter script occupies 19333 quadwords. Native interpreter arrays
    // need 896 more, while the retail arrays need 448 and fit the 20000 budget.
    char        *bytes = reinterpret_cast<char *>(memory.Alloc(19333));
    EventProgram program;
    std::memcpy(bytes, &program, sizeof(program));
    SetEventScript(std::span<u8>(reinterpret_cast<u8 *>(bytes), sizeof program), &memory);
    EXPECT_NE(EventScript.prog, nullptr);
    EXPECT_EQ(RunEvent(100, nullptr), 0);
    EXPECT_EQ(memory.stack_used, 19333);
    SetEventScript({}, nullptr);
    EXPECT_EQ(RunEvent(100, nullptr), -1);
}

TEST(EventRuntimeMemory, ReplacingAProgramRetainsTheInterpreterStackCapacity) {
    EventProgram first;
    EventProgram second;
    second.entry.no = 101;
    alignas(64) u_long128 storage[1024]{};
    mgCMemory             memory;
    memory.stSetBuffer(storage, 1024);
    SetEventScript(ProgramBytes(first), &memory);
    EXPECT_EQ(RunEvent(100, nullptr), 0);
    SetEventScript(ProgramBytes(second), &memory);
    EXPECT_EQ(RunEvent(101, nullptr), 0);
    EXPECT_EQ(RunEvent(100, nullptr), -1);
    SetEventScript({}, nullptr);
}

TEST(EventScriptHooks, SceneEventEntryCanBeReplacedBeforeNativeExecution) {
    TempRoot root;
    const fs::path package = root.path / "mods" / "test.event-hook-entry";
    Write(package / "mod.json",
          R"json({"schema_version":1,"id":"test.event-hook-entry","name":"Event Hook Entry","version":"1.0.0","framework_api":1,"games":["chronicle-two"],"entry":"main.lua","permissions":["game.read","game.write","scripts.override"]})json");
    Write(package / "main.lua", R"lua(
local mode = "replace"
local initial = assert(game.scripts.override({id = "native.event.run"}, function(context)
  assert(context.game == "chronicle-two" and context.program_no == 100)
  if mode == "call" then
    local called = assert(game.scripts.call({id = context.hook_id}))
    assert(called.run_status == 0)
    local repeated, repeated_error = game.scripts.call({id = context.hook_id})
    assert(repeated == nil and repeated_error.code == "phase_violation")
    moonmod.log("info", "MOONMOD_C2_NATIVE_EVENT_ENTRY_CALLED")
    return {handled = true}
  end
  moonmod.log("info", "MOONMOD_C2_NATIVE_EVENT_ENTRY_REPLACED")
  return {handled = true}
end))
assert(initial.replaced == false)
game.events.subscribe("tests.event_hook_mode", function(event)
  if event.mode == "call" then mode = "call" end
end)
)lua");
    Write(root.path / "save" / "moonmod" / "profile.json",
          R"json({"format_version":1,"enabled":{"test.event-hook-entry":true},"settings":{}})json");

    std::vector<std::string> logs;
    moonmod::Runtime &runtime = moonmod::SharedRuntime();
    runtime.Stop();
    moonmod::RuntimeConfig config;
    config.game_id = "chronicle-two";
    config.game_name = "Chronicle Two";
    config.mod_directories = {root.path / "mods"};
    config.save_root = root.path / "save";
    config.log = [&](std::string_view, std::string_view, std::string_view message) { logs.emplace_back(message); };
    std::string error;
    ASSERT_TRUE(runtime.Start(std::move(config), error)) << error;

    alignas(64) u_long128 storage[1024]{};
    mgCMemory memory;
    memory.stSetBuffer(storage, 1024);
    EventProgram program;
    char *bytes = reinterpret_cast<char *>(memory.Alloc(static_cast<int>((sizeof(program) + 15) / 16)));
    std::memcpy(bytes, &program, sizeof(program));
    SetEventScript(std::span<u8>(reinterpret_cast<u8 *>(bytes), sizeof program), &memory);
    EventScript.end = 0;

    EXPECT_EQ(RunEvent(100, nullptr), 0);
    EXPECT_EQ(EventScript.end, 0) << "handled=true must skip EventScript.run";
    EXPECT_EQ(std::count(logs.begin(), logs.end(), "MOONMOD_C2_NATIVE_EVENT_ENTRY_REPLACED"), 1);

    ASSERT_TRUE(runtime.PublishToMod("test.event-hook-entry", "tests.event_hook_mode", {{"mode", "call"}}));
    EventScript.end = 0;
    EXPECT_EQ(RunEvent(100, nullptr), 0);
    EXPECT_EQ(EventScript.end, 1) << "scripts.call must execute the native event program exactly once";
    EXPECT_EQ(std::count(logs.begin(), logs.end(), "MOONMOD_C2_NATIVE_EVENT_ENTRY_CALLED"), 1)
        << testing::PrintToString(logs);

    SetEventScript({}, nullptr);
    runtime.Stop();
}
