# darkchronicle_tests links the whole port and game so a test can exercise a replacement unit against the
# real game types. Each *_test.cpp holds GoogleTest cases; ctest runs one process per case, since the
# game's state is global.
include(FetchContent)
FetchContent_Declare(googletest
    URL https://codeload.github.com/google/googletest/tar.gz/refs/tags/v1.18.0
    URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    FIND_PACKAGE_ARGS 1.18.0 EXACT NAMES GTest)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

if(NOT CT_SANITIZERS STREQUAL "none")
    add_test(NAME Sanitizers.Runtime
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/cmake/test_sanitizers.py
            --compiler ${CMAKE_CXX_COMPILER} --sanitizers ${CT_SANITIZERS})
    set_tests_properties(Sanitizers.Runtime PROPERTIES TIMEOUT 90)
endif()

add_test(NAME PalettePaths.Check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/port/check_palette_paths.py
        ${CMAKE_SOURCE_DIR}/port/src)
add_test(NAME NativeArchitecture.Check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/port/native_architecture.py
        --root ${CMAKE_SOURCE_DIR} --build ${CMAKE_BINARY_DIR})

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
add_executable(darkchronicle_tests ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${TEST_SOURCES})
# MoonMod's native plugin fixtures: one linked into the tests, one loaded as a shared library at run time.
add_library(moonmod_native_plugin_fixture OBJECT moonmod_native_plugin_fixture.c)
set_target_properties(moonmod_native_plugin_fixture PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
target_include_directories(moonmod_native_plugin_fixture PRIVATE ${CMAKE_SOURCE_DIR}/port/include)
target_sources(darkchronicle_tests PRIVATE $<TARGET_OBJECTS:moonmod_native_plugin_fixture>)
add_library(moonmod_native_plugin_shared_fixture SHARED moonmod_native_plugin_shared_fixture.c)
set_target_properties(moonmod_native_plugin_shared_fixture PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(moonmod_native_plugin_shared_fixture PRIVATE ${CMAKE_SOURCE_DIR}/port/include)
foreach(fixture IN ITEMS moonmod_native_plugin_fixture moonmod_native_plugin_shared_fixture)
    target_compile_options(${fixture} PRIVATE ${CT_WARNINGS})
endforeach()
target_compile_definitions(darkchronicle_tests PRIVATE
    MOONMOD_TEST_NATIVE_PLUGIN_PATH="$<TARGET_FILE:moonmod_native_plugin_shared_fixture>"
    MOONMOD_TEST_GAME_ID="chronicle-two"
    MOONMOD_TEST_CHRONICLE_TWO=1)
set(MOONFACTORY_FIRST_STEPS_PATH "" CACHE PATH "MoonFactory First Steps example package")
if(NOT MOONFACTORY_FIRST_STEPS_PATH)
    foreach(framework_dir IN ITEMS MoonMod MoonFactory)
        if(EXISTS "${CMAKE_SOURCE_DIR}/../${framework_dir}/examples/first-steps/mod.json")
            set(MOONFACTORY_FIRST_STEPS_PATH "${CMAKE_SOURCE_DIR}/../${framework_dir}/examples/first-steps")
            break()
        endif()
    endforeach()
endif()
if(EXISTS "${MOONFACTORY_FIRST_STEPS_PATH}/mod.json")
    file(TO_CMAKE_PATH "${MOONFACTORY_FIRST_STEPS_PATH}" MOONFACTORY_FIRST_STEPS_PATH)
    target_compile_definitions(darkchronicle_tests PRIVATE
        MOONFACTORY_FIRST_STEPS_PATH="${MOONFACTORY_FIRST_STEPS_PATH}")
endif()
add_dependencies(darkchronicle_tests moonmod_native_plugin_shared_fixture)
if(WIN32)
    include(${KZ_ROOT}/cmake/KazarovWindowsRuntime.cmake)
    kz_windows_runtime_post_build(darkchronicle_tests)
endif()
# Decoder fixtures remain available in tests without linking proprietary texture decoding into the game.
target_sources(darkchronicle_tests PRIVATE ${KZ_ROOT}/src/cpp/gfx/tim2.cpp)
set_target_properties(darkchronicle_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(darkchronicle_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/config_parity_test.cpp
    PROPERTIES INCLUDE_DIRECTORIES "${CT_STB_IMAGE_INCLUDE_DIR}")
set(LOCAL_LAYOUT_PROBES ${CMAKE_CURRENT_BINARY_DIR}/local_layout_probes.hpp)
add_custom_command(OUTPUT ${LOCAL_LAYOUT_PROBES}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/port/local_layout_probes.py
        ${CMAKE_SOURCE_DIR} ${LOCAL_LAYOUT_PROBES}
    DEPENDS ${CMAKE_SOURCE_DIR}/scripts/port/local_layout_probes.py
        ${CMAKE_SOURCE_DIR}/port/src/object/menu/menusys.cpp
    VERBATIM)
target_sources(darkchronicle_tests PRIVATE ${LOCAL_LAYOUT_PROBES})
target_include_directories(darkchronicle_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
ct_link_game(darkchronicle_tests)
kz_port_tests(darkchronicle_tests)
target_link_libraries(darkchronicle_tests PRIVATE GTest::gtest nlohmann_json::nlohmann_json ${CMAKE_DL_LIBS})
# The painters' geometry is checked against the numbers the fonts were fitted with.
target_compile_definitions(darkchronicle_tests PRIVATE
    CT_MESSAGE_FONT_PARAMETERS="${CMAKE_SOURCE_DIR}/scripts/font/message_font.json"
    CT_MENU_FONT_PARAMETERS="${CMAKE_SOURCE_DIR}/scripts/font/menu_font.json"
    CT_BOARD_FONT_PARAMETERS="${CMAKE_SOURCE_DIR}/scripts/font/board_font.json")

# The shipped message font: its grid and coverage, and, with Kosugi at hand, that the tools rebuild it
# byte for byte. Skips without the tools' Python packages (scripts/font/requirements.txt).
add_test(NAME MessageFont.Check COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/font/test_message_font.py)
# The shipped menu font: its characters and flared stems, and, with Libre Franklin at hand, that the tools
# rebuild it byte for byte; and the time board's font likewise.
add_test(NAME MenuFont.Check COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/font/test_menu_font.py)

# A case is a unit test: it finishes in milliseconds, and one that does not is cut off. The clock
# cases measure short sleeps, which the parallel gfx cases skew, so they run on their own. The cases that compile
# pipelines or create a device per case take longer than 5 s under a parallel run, on lavapipe and on Windows, so they get at least 30 s, or CT_TEST_TIMEOUT when it is larger.
# Cases that need a Vulkan device carry the gpu label: hosted runners have none and exclude them with -LE gpu.
set(CT_TEST_TIMEOUT 5 CACHE STRING "Seconds a test case may run before it is cut off")
set(CT_GPU_SLOW_TIMEOUT 30 CACHE STRING "Seconds a case in CT_GPU_SLOW_TESTS may run before it is cut off")
if(CT_TEST_TIMEOUT GREATER CT_GPU_SLOW_TIMEOUT)
    set(CT_GPU_SLOW_TIMEOUT ${CT_TEST_TIMEOUT})
endif()
# These cases share renderer suites but only inspect CPU data and must run on hosted CI.
set(CT_CPU_TESTS
    GameplayTimingSites.*
    GameplayTimingChapter.*
    MgVisual.FaceMeshListAndStrip
    MglibProjection.DepthRoundTrip
    ScreenEffect.DepthOfFieldLevelSizesFollowRetailCascade
    Vu1Prog0Shader.MirrorWordVectors
    GfxMeshDc2.MeshConstantsKeepChroniclesSize)
set(CT_GPU_SLOW_TESTS
    CameraFixedMouseGpu.*
    FontPortGpu.*
    Vu1Prog0Shader.*
    GfxGpu*.*
    */GfxGpu*.*
    MgTextureGpu.*
    MenuIconGpu.*
    MgTanimeGpu.*
    NativeRenderState.*
    PersistentMesh.*
    GfxMisc.PipelineCache
    MovieGlue.ThirtyFramesOfL5LogoThroughTheMovieTexture
    MovieTextureGpu.*)
set(CT_GPU_TESTS
    TargetPresentation.*
    PickupPresentation.RetailEmissionsTagCoinsAbsAndFloatingItems
    HealingPresentation.*
    DamagePresentation.RetailDamageInterpolatesProjectedBounceAndOpacity
    DamagePresentation.Score2InterpolatesAndReusedSlotsStayNewest
    DamagePresentation.SpriteHealClippingAndPause
    DamagePresentation.RealAtlasProjectedEndpoints
    QuickChangeBannerPresentation.*
    MglibAspect.*
    MglibPresent.*
    MglibSeam.*
    MgVisual.*
    ScreenEffect.*
    MgShadow.*
    Water.*
    MglibFrame.*
    MglibState.*
    MglibProjection.*
    MglibFrameDeathTest.*
    Photo.*
    MgDrawPrimRender.*
    MenuDrawGrab.*
    GfxMisc.*
    */GfxColorWriteMask.*
    GfxCachedTarget.*
    GfxTim2.UploadRgbaAndIndexed
    GfxValidation.DeliberateMisuseIsCaught)
# These cases write and scan a catalogue of 31 saves; a save holds SAVE_DUNGEON_FLOOR_NUM floor records and
# USED_DATA_PLACE_MAX places (port/include/capacity.hpp), so the catalogue outlasts CT_TEST_TIMEOUT.
set(CT_CPU_SLOW_TESTS
    SaveSlotTest.CatalogHasNoRetailSlotLimitAndOnlyOneNewRow
    SaveSlotTest.MenuScrollsPastThirteenAndClampsItsVisibleWindow)
set(CT_CPU_SLOW_TIMEOUT 20 CACHE STRING "Seconds a case in CT_CPU_SLOW_TESTS may run before it is cut off")
if(CT_TEST_TIMEOUT GREATER CT_CPU_SLOW_TIMEOUT)
    set(CT_CPU_SLOW_TIMEOUT ${CT_TEST_TIMEOUT})
endif()
list(JOIN CT_GPU_SLOW_TESTS ":" CT_GPU_SLOW_FILTER)
list(JOIN CT_GPU_TESTS ":" CT_GPU_FILTER)
list(JOIN CT_CPU_TESTS ":" CT_CPU_FILTER)
list(JOIN CT_CPU_SLOW_TESTS ":" CT_CPU_SLOW_FILTER)
set(CT_CONFIG_PARITY_FILTER "NativeConfigParity.*")
set(CT_CORPUS_FILTER "NativeSubtitles.MatchesEveryOfflineSubtitleChapter:MovieGlue.L5LogoPlaysOnTheClockWithItsSoundtrack:SoundRealData.PublishedBanksAndSequencesLoad")
set(CT_CORPUS_TIMEOUT 120)
if(CT_TEST_TIMEOUT GREATER CT_CORPUS_TIMEOUT)
    set(CT_CORPUS_TIMEOUT ${CT_TEST_TIMEOUT})
endif()
# The original configuration corpus also compares every embedded texture mip and variant.
set(CT_CONFIG_PARITY_TIMEOUT 600)
if(CT_TEST_TIMEOUT GREATER CT_CONFIG_PARITY_TIMEOUT)
    set(CT_CONFIG_PARITY_TIMEOUT ${CT_TEST_TIMEOUT})
endif()

# Catch direct device helpers and renderer-owning fixtures in new test suites at configure time.
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/port/gpu_test_labels.py
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tools/kazarov/src/cpp/tests
    --filters ${CT_GPU_FILTER}:${CT_GPU_SLOW_FILTER}
    --cpu-filters ${CT_CPU_FILTER}
    RESULT_VARIABLE gpu_label_result OUTPUT_VARIABLE gpu_label_output ERROR_VARIABLE gpu_label_error)
if(NOT gpu_label_result EQUAL 0)
    message(FATAL_ERROR "${gpu_label_output}${gpu_label_error}")
endif()
# Listing the cases runs the binary's static initialisers, which can outlast CMake's 5 second default on a
# loaded CI runner.
gtest_discover_tests(darkchronicle_tests
    DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_TIMEOUT 60
    TEST_FILTER -PlatformClock.*:${CT_GPU_FILTER}:${CT_GPU_SLOW_FILTER}:${CT_CPU_FILTER}:${CT_CPU_SLOW_FILTER}:${CT_CORPUS_FILTER}:${CT_CONFIG_PARITY_FILTER}
    PROPERTIES TIMEOUT ${CT_TEST_TIMEOUT})
gtest_discover_tests(darkchronicle_tests
    DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_TIMEOUT 60
    TEST_FILTER ${CT_CPU_SLOW_FILTER}
    PROPERTIES TIMEOUT ${CT_CPU_SLOW_TIMEOUT})
gtest_discover_tests(darkchronicle_tests
    DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_TIMEOUT 60
    TEST_FILTER PlatformClock.*
    PROPERTIES TIMEOUT ${CT_TEST_TIMEOUT} RUN_SERIAL TRUE)
gtest_discover_tests(darkchronicle_tests
    DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_TIMEOUT 60
    TEST_FILTER ${CT_CORPUS_FILTER}
    PROPERTIES TIMEOUT ${CT_CORPUS_TIMEOUT} RUN_SERIAL TRUE LABELS "cpu;corpus")
gtest_discover_tests(darkchronicle_tests
    DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_TIMEOUT 60
    TEST_FILTER ${CT_CONFIG_PARITY_FILTER}
    PROPERTIES TIMEOUT ${CT_CONFIG_PARITY_TIMEOUT} RUN_SERIAL TRUE LABELS "cpu;corpus")
gtest_discover_tests(darkchronicle_tests
    DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_TIMEOUT 60
    TEST_FILTER ${CT_CPU_FILTER}
    PROPERTIES TIMEOUT ${CT_TEST_TIMEOUT} LABELS cpu)

gtest_discover_tests(darkchronicle_tests
    DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_TIMEOUT 60
    TEST_FILTER ${CT_GPU_FILTER}-${CT_GPU_SLOW_FILTER}:${CT_CPU_FILTER}
    PROPERTIES TIMEOUT ${CT_TEST_TIMEOUT} LABELS gpu RESOURCE_LOCK renderer FIXTURES_REQUIRED gpu_pipeline_cache)
gtest_discover_tests(darkchronicle_tests
    DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_TIMEOUT 60
    TEST_FILTER ${CT_GPU_SLOW_FILTER}-${CT_CPU_FILTER}
    PROPERTIES TIMEOUT ${CT_GPU_SLOW_TIMEOUT} LABELS gpu RESOURCE_LOCK renderer FIXTURES_REQUIRED gpu_pipeline_cache)

add_dependencies(darkchronicle_tests darkchronicle)
# ctest reports a process that dies by abort() as failed even when PASS_REGULAR_EXPRESSION matches its
# output, so the executable runs under a script that checks the run and the file it writes, and the regex
# judges the script's verdict. Its shader compile loads the CPU, so it runs alone with the clock cases.
# The smoke fixture is coherent empty publisher output at the pinned data version, so the run
# passes the first-run data check and stays in the logo fade without game data.
set(SMOKE_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/smoke_headless.cmake)
file(WRITE ${SMOKE_SCRIPT} [=[
include("${EMPTY_DATA_FIXTURE}")
ct_empty_data_fixture("${ROOT}" "${DATA_VERSION_SOURCE}")
file(MAKE_DIRECTORY "$ENV{DC2_SAVE}")
file(REMOVE "$ENV{DC2_SAVE}/smoke.png")
execute_process(COMMAND "${EXE}" --headless --offscreen --frames 3 --screenshot "$ENV{DC2_SAVE}/smoke.png"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
set(log "${out}${err}")
message("${log}")
set(png "$ENV{DC2_SAVE}/smoke.png")
set(sig "")
if(EXISTS "${png}")
    file(READ "${png}" sig LIMIT 8 HEX)
endif()
string(FIND "${log}" "dataread: indexed " index_at)
string(FIND "${log}" "mglib: frame 3 written to" frame_at)
if(rc EQUAL 0 AND index_at GREATER -1 AND frame_at GREATER -1 AND sig STREQUAL "89504e470d0a1a0a")
    message("SMOKE_OK")
endif()
]=])
add_test(NAME Smoke.HeadlessFramesScreenshot
    COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:darkchronicle>
        -DROOT=${CMAKE_BINARY_DIR}/smoke-fixture
        -DEMPTY_DATA_FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/empty_data.cmake
        -DDATA_VERSION_SOURCE=${KZ_ROOT}/data_version.json -P ${SMOKE_SCRIPT})
set_tests_properties(Smoke.HeadlessFramesScreenshot PROPERTIES
    PASS_REGULAR_EXPRESSION "SMOKE_OK"
    ENVIRONMENT "DC2_SAVE=${CMAKE_BINARY_DIR}/smoke-fixture/save;DC2_DATA=${CMAKE_BINARY_DIR}/smoke-fixture/data"
    TIMEOUT 120 RUN_SERIAL TRUE LABELS gpu)

# MoonMod: a headless launch, twice, with a probe package that exercises the API catalog, content registration,
# storage and save migration; the second launch checks what the first one persisted. It runs beside the plain smoke
# test on its own fixture.
set(MOONMOD_SMOKE_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/moonmod_smoke_headless.cmake)
file(WRITE ${MOONMOD_SMOKE_SCRIPT} [===[
include("${EMPTY_DATA_FIXTURE}")
ct_empty_data_fixture("${ROOT}" "${DATA_VERSION_SOURCE}")
file(MAKE_DIRECTORY "$ENV{DC2_DATA}/map")
set(map_catalog_fixture [=[
{"schema_version":2,"kind":"map_names","count":2,"maps":[
 {"name":"probe-town","title":"Moon Factory Fixture Town","add_path":"","type":1,"sel_type":1,"snd_data_id":-1,"area_no":2},
 {"name":"probe-field","title":"Moon Factory Fixture Field","add_path":"","type":4,"sel_type":3,"snd_data_id":-1,"area_no":4}]}
]=])
file(WRITE "$ENV{DC2_DATA}/map/map1.cfg.json" "${map_catalog_fixture}")
file(WRITE "$ENV{DC2_DATA}/map/map2.cfg.json" "${map_catalog_fixture}")
file(MAKE_DIRECTORY "$ENV{DC2_SAVE}")
file(MAKE_DIRECTORY "$ENV{DC2_SAVE}/mods/moonmod.headless-probe")
file(MAKE_DIRECTORY "$ENV{DC2_SAVE}/mods/moonmod.headless-probe/resources")
file(MAKE_DIRECTORY "$ENV{DC2_SAVE}/moonmod")
file(WRITE "$ENV{DC2_SAVE}/mods/moonmod.headless-probe/resources/probe.bin" "ok")
file(WRITE "$ENV{DC2_SAVE}/mods/moonmod.headless-probe/resources/moon.map" "map")
file(WRITE "$ENV{DC2_SAVE}/mods/moonmod.headless-probe/resources/moon.cfg" "cfg")
file(WRITE "$ENV{DC2_SAVE}/mods/moonmod.headless-probe/resources/moon.mpk" "mpk")
file(WRITE "$ENV{DC2_SAVE}/mods/moonmod.headless-probe/resources/moon.ipk" "ipk")
file(REMOVE_RECURSE "$ENV{DC2_SAVE}/moonmod/data/moonmod.headless-probe")
file(WRITE "$ENV{DC2_SAVE}/mods/moonmod.headless-probe/mod.json" [=[
  {"schema_version":1,"id":"moonmod.headless-probe","name":"MoonMod Headless Probe","version":"1.0.0","framework_api":1,"games":["chronicle-two"],"entry":"main.lua","permissions":["content.register","game.read","game.write","storage.read","storage.write","debug.inspect"]}
]=])
file(WRITE "$ENV{DC2_SAVE}/mods/moonmod.headless-probe/main.lua" [=[
local info = assert(game.info.get())
assert(info.id == "chronicle-two" and info.api_major == 1)
local info_by_path = assert(moonmod.api.call("info.get"))
assert(info_by_path.id == info.id)
local catalog = game.api.list()
assert(type(catalog) == "table" and #catalog >= 70)
local domains = {}
for _, operation in ipairs(catalog) do
  assert(operation.domain_api_major == 1)
  domains[operation.domain] = true
  moonmod.log("info", "MOONMOD_API_STATUS|" .. operation.path .. "|" .. operation.status .. "|" .. operation.reason)
end
moonmod.log("info", "MOONMOD_API_CATALOG_COUNT|" .. #catalog)
for _, domain in ipairs({"info", "events", "player", "party", "world", "locations", "dungeons", "georama", "entities", "combat", "items",
                         "skills", "models", "animations",
                         "weapons", "characters", "quests", "scripts", "content", "save", "input", "audio",
                         "render", "ui", "physics", "resources", "localization", "commands", "compat", "debug"}) do
  assert(domains[domain], "missing API domain: " .. domain)
end
for _, path in ipairs({"combat.styles_list",
                         "combat.styles_get", "combat.styles_register", "combat.styles_set_active"}) do
  local supported, reason = moonmod.api.supports(path)
  assert(not supported and type(reason) == "string" and #reason > 0, path .. " must remain explicitly unsupported until native verification")
end
do
  local supported, reason = moonmod.api.supports("locations.register")
  assert(supported and reason == "", "Chronicle Two must expose native-backed location registration")
end
local required_map_assets = {"map", "cfg", "mpk", "ipk"}
for _, extension in ipairs(required_map_assets) do
  local override = assert(game.resources.override({
    path = "resources/moon." .. extension,
    game_path = "map/m/moo/npr/moonprobe." .. extension
  }))
  assert(override.status == "registered")
end
local mod_location = assert(game.locations.register({
  id = "moon-cave", map_name = "moonprobe", name = "Moon Factory Cave",
  texture_source_map = "probe-field"
}))
assert(mod_location.id == "chronicle-two:location:moonmod.headless-probe:moon-cave")
assert(mod_location.map_name == "moonprobe" and mod_location.texture_source_map == "probe-field" and
       (mod_location.status == "pending" or mod_location.status == "registered"))
moonmod.log("info", "MOONMOD_HEADLESS_LOCATION_REGISTERED|" .. mod_location.id)
for _, path in ipairs({"animations.register", "animations.play", "animations.stop"}) do
  local supported, reason = moonmod.api.supports(path)
  assert(supported and reason == "", path .. " must be supported by the Chronicle Two native motion adapter")
end
do
for _, path in ipairs({"models.replace", "models.remove", "models.unregister"}) do
  local supported, reason = moonmod.api.supports(path)
  assert(supported and reason == "", path .. " must be supported by the native model binding adapter")
end
local model_supported, model_reason = moonmod.api.supports("models.resolve")
assert(model_supported and model_reason == "")
model_supported, model_reason = moonmod.api.supports("models.register")
assert(model_supported and model_reason == "")
local model = assert(game.models.resolve({monster_id = 0}))
assert(model.model == "e201a" and model.source == "native_table")
assert(model.assets[1].game_path == "dungeon/monster/e201a.chr" and not model.assets[1].overridden)
local model_override = assert(game.resources.override({
  path = "resources/probe.bin", game_path = "dungeon/monster/moon_probe.chr"
}))
assert(model_override.status == "registered")
local replaced_model = assert(game.models.replace({monster_id = 0, model = "moon_probe"}))
assert(replaced_model.changed and replaced_model.model == "moon_probe" and
       replaced_model.game_path == "dungeon/monster/moon_probe.chr")
local overridden_model = assert(game.models.resolve({monster_id = 0}))
assert(overridden_model.model == "moon_probe" and overridden_model.source == "package_override" and
       overridden_model.assets[1].overridden)
local removed_model = assert(game.models.remove({monster_id = 0}))
assert(removed_model.removed and removed_model.model == "e201a")
local restored_model = assert(game.models.resolve({monster_id = 0}))
assert(restored_model.model == "e201a" and restored_model.source == "native_table" and
       not restored_model.assets[1].overridden)
assert(game.resources.unoverride({game_path = "dungeon/monster/moon_probe.chr"}).removed)
assert(assert(game.models.resolve({monster_id = 0})).source == "native_table")
moonmod.log("info", "MOONMOD_HEADLESS_MODEL_RESOLVE_OK")
moonmod.log("info", "MOONMOD_HEADLESS_MODEL_REPLACE_OK")
end
assert(game.resources.override({path = "resources/probe.bin", game_path = "dungeon/monster/moonprobe.chr"}))
model = assert(game.models.register({
  id = "headless-model", template_monster_id = 0, model = "moonprobe"
}))
model_reason = model.id
assert(model.status == "registered" and model.model == "moonprobe" and
       model.assets[1].game_path == "dungeon/monster/moonprobe.chr" and model.assets[1].overridden)
local unregistered_model = assert(game.models.unregister({id = "headless-model"}))
assert(unregistered_model.removed and unregistered_model.id == model.id and unregistered_model.type == "model")
model, model_reason = nil, nil
model = assert(game.models.register({
  id = "headless-model", template_monster_id = 0, model = "moonprobe"
}))
model_reason = model.id
overridden_model = assert(game.models.resolve({id = model_reason}))
assert(overridden_model.model == "moonprobe" and overridden_model.source == "package_override")
model_override = assert(game.characters.register({
  id = "headless-model-character", kind = "enemy", name = "Headless Model Character", model = model_reason
}))
assert(model_override.model == "moonprobe" and model_override.model_id == model_reason and
       model_override.native_monster_id > 0)
model = assert(game.models.resolve({monster_id = model_override.native_monster_id}))
assert(model.model == "moonprobe" and model.model_id == model_reason and model.source == "package_override")
local referenced_model, referenced_model_error = game.models.unregister({id = "headless-model"})
assert(referenced_model == nil and referenced_model_error.code == "failed" and
       referenced_model_error.message:match("registered character uses this model binding"))
moonmod.log("info", "MOONMOD_HEADLESS_MODEL_UNREGISTER_OK")
moonmod.log("info", "MOONMOD_HEADLESS_MODEL_REGISTER_OK|" .. model_reason)
do
local animation_register_supported, animation_register_reason = moonmod.api.supports("animations.register")
local animation_play_supported, animation_play_reason = moonmod.api.supports("animations.play")
local animation_stop_supported, animation_stop_reason = moonmod.api.supports("animations.stop")
assert(animation_register_supported and animation_register_reason == "")
assert(animation_play_supported and animation_play_reason == "")
assert(animation_stop_supported and animation_stop_reason == "")
local animation = assert(game.animations.register({id = "headless-idle", model = model_reason, motion_index = 0}))
assert(animation.id == "moonmod.headless-probe:headless-idle" and animation.model == model_reason and
       animation.motion_index == 0 and animation.status == "registered" and
       animation.motion_validation == "deferred_until_model_is_loaded")
local no_dungeon_play, no_dungeon_play_error = game.animations.play({
  animation = "headless-idle", entity = "chronicle-two:monster:0:1"
})
assert(no_dungeon_play == nil and no_dungeon_play_error.code == "failed" and
       no_dungeon_play_error.message:match("characters.spawn"))
local no_dungeon_stop, no_dungeon_stop_error = game.animations.stop({entity = "chronicle-two:monster:0:1"})
assert(no_dungeon_stop == nil and no_dungeon_stop_error.code == "failed" and
       no_dungeon_stop_error.message:match("no active MoonMod animation"))
moonmod.log("info", "MOONMOD_HEADLESS_ANIMATIONS_NO_DUNGEON_OK|" .. animation.id)
end
local georama_supported, georama_reason = moonmod.api.supports("georama.get")
assert(georama_supported and georama_reason == "")
local piece_register_supported, piece_register_reason = moonmod.api.supports("georama.register")
assert(piece_register_supported and piece_register_reason == "")
local piece, piece_error = game.georama.register({id = "headless-piece", name = "Headless Piece", template_part_id = 1})
assert(piece ~= nil and piece.status == "pending" and piece_error == nil)
local condition_supported, condition_reason = moonmod.api.supports("georama.condition_set")
assert(condition_supported and condition_reason == "")
local request_known_supported, request_known_reason = moonmod.api.supports("georama.request_known_set")
assert(request_known_supported and request_known_reason == "")
local request_set_supported, request_set_reason = moonmod.api.supports("georama.request_set")
assert(request_set_supported and request_set_reason == "")
local piece_place_supported, piece_place_reason = moonmod.api.supports("georama.place")
assert(piece_place_supported and piece_place_reason == "")
local no_active_place, no_active_place_error = game.georama.place({
  id = "moonmod.headless-probe:headless-piece", position = {x = 0, y = 0, z = 0}, rotation_steps = 0
})
assert(no_active_place == nil and no_active_place_error.code == "failed")
local piece_remove_supported, piece_remove_reason = moonmod.api.supports("georama.remove")
assert(piece_remove_supported and piece_remove_reason == "")
local no_active_remove, no_active_remove_error = game.georama.remove({
  placement_id = "chronicle-two:georama-placement:0:0"
})
assert(no_active_remove == nil and no_active_remove_error.code == "failed")
local river_set_supported, river_set_reason = moonmod.api.supports("georama.river_set")
assert(river_set_supported and river_set_reason == "")
local no_active_river, no_active_river_error = game.georama.river_set({
  grid_index = 0, x = 0, z = 0, has_river = true
})
assert(no_active_river == nil and no_active_river_error.code == "failed")
local active_town, active_town_error = game.georama.get()
assert(active_town == nil and active_town_error.code == "failed")
local saved_town, saved_town_error = game.georama.get({town_id = 0})
assert(saved_town == nil and saved_town_error.code == "failed")
local condition, condition_error = game.georama.condition_set({town_id = 0, condition_id = 0, met = true})
assert(condition == nil and condition_error.code == "failed")
local request_known, request_known_error = game.georama.request_known_set({town_id = 0, request_id = 0, known = true})
assert(request_known == nil and request_known_error.code == "failed")
local request, request_error = game.georama.request_set({town_id = 0, request_id = 0, completed = true})
assert(request == nil and request_error.code == "failed")
moonmod.log("info", "MOONMOD_HEADLESS_GEORAMA_NO_SAVE")
moonmod.log("info", "MOONMOD_HEADLESS_FUTURE_DOMAINS_UNSUPPORTED")
for _, path in ipairs({"scripts.register_function", "scripts.list_functions", "scripts.call_function", "scripts.unregister_function"}) do
  local supported, reason = moonmod.api.supports(path)
  assert(supported and reason == "", path)
end
assert(#assert(game.scripts.list_functions()).functions == 0)
local exported_function = assert(game.scripts.register_function({id = "double"}, function(arguments)
  return arguments.value * 2
end))
assert(exported_function.id == "moonmod.headless-probe:double" and exported_function.exposed)
local listed_functions = assert(game.scripts.list_functions())
assert(#listed_functions.functions == 1 and listed_functions.functions[1].id == exported_function.id)
assert(listed_functions.functions[1].owner_mod_id == "moonmod.headless-probe")
assert(assert(game.scripts.call_function({id = "double", arguments = {value = 21}})) == 42)
assert(game.scripts.unregister_function({id = "double"}).removed)
local removed_function, removed_function_error = game.scripts.call_function({id = "double"})
assert(removed_function == nil and removed_function_error.code == "failed")
moonmod.log("info", "MOONMOD_HEADLESS_SCRIPT_FUNCTIONS_OK")
for _, path in ipairs({"skills.list", "skills.get", "skills.register", "skills.unlock", "skills.set_rank"}) do
  local supported, reason = moonmod.api.supports(path)
  assert(supported and reason == "", path)
end
local prior_skill = game.skills.get({id = "survival.focus"})
local skill_tree = assert(game.skills.register({
  id = "survival", title = "Survival",
  skills = {
    {id = "focus", title = "Focus", max_rank = 2},
    {id = "scavenging", title = "Scavenging", max_rank = 1, prerequisites = {{id = "focus", rank = 1}}}
  }
}))
assert(skill_tree.id == "moonmod.headless-probe:survival")
assert(skill_tree.idempotent == (prior_skill ~= nil))
if prior_skill == nil then
  assert(assert(game.skills.unlock({id = "survival.focus"})).rank == 1)
  assert(assert(game.skills.unlock({id = "survival.scavenging"})).rank == 1)
else
  assert(prior_skill.rank == 1)
  assert(assert(game.skills.get({id = "survival.scavenging"})).rank == 1)
end
assert(#assert(game.skills.list()).skills == 2)
moonmod.log("info", "MOONMOD_HEADLESS_SKILLS_OK")
for _, path in ipairs({"quests.get", "quests.register", "quests.start", "quests.advance"}) do
  local supported, reason = moonmod.api.supports(path)
  assert(supported and reason == "", path)
end
moonmod.log("info", "MOONMOD_HEADLESS_QUEST_CAPABILITIES_OK")
local resource = assert(game.resources.load({path = "resources/probe.bin"}))
assert(resource.status == "loaded" and resource.size == 2 and resource.media_type == "application/octet-stream")
local resource_data = assert(game.resources.resolve({handle = resource.handle}))
assert(resource_data.encoding == "base64" and resource_data.data == "b2s=")
assert(game.resources.release({handle = resource.handle}).removed)
moonmod.log("info", "MOONMOD_HEADLESS_RESOURCES_OK")
local notice = assert(game.ui.notify({message = "Headless probe ready", duration_frames = 120}))
assert(notice.queued and notice.manager_open == false and notice.duration_frames == 120)
moonmod.log("info", "MOONMOD_HEADLESS_UI_NOTIFY_OK")
local ui_register_supported = assert(moonmod.api.supports("ui.register"))
local ui_remove_supported = assert(moonmod.api.supports("ui.remove"))
assert(ui_register_supported and ui_remove_supported)
local menu_action = assert(game.ui.register({id = "smoke-action", label = "Smoke action", description = "API probe"}))
assert(menu_action.id == "moonmod.headless-probe:smoke-action" and menu_action.status == "registered")
assert(game.ui.remove({id = "smoke-action"}).removed)
moonmod.log("info", "MOONMOD_HEADLESS_UI_ACTIONS_OK")
local input_binding = assert(game.input.bind({id = "smoke-action", keys = {"K", "Gamepad:back"}}))
assert(input_binding.id == "moonmod.headless-probe:smoke-action")
assert(not assert(game.input.state({id = "smoke-action"})).down)
assert(not assert(game.input.consume({id = "smoke-action"})).pressed)
assert(game.input.unbind({id = "smoke-action"}).removed)
do
  local pushed = assert(game.input.context_push({id = "smoke-modal"}))
  local context = assert(game.input.context_get())
  assert(pushed.id == "moonmod.headless-probe:smoke-modal")
  assert(context.active == pushed.id and context.depth == 1 and #context.stack == 1)
  assert(game.input.context_pop({token = pushed.token}).active == "default")
end
moonmod.log("info", "MOONMOD_HEADLESS_INPUT_CONTEXTS_OK")
do
  local velocity, velocity_failure = game.physics.velocity_get({entity = "chronicle-two:monster:0:1"})
  assert(velocity == nil and velocity_failure.code == "failed")
end
moonmod.log("info", "MOONMOD_HEADLESS_PHYSICS_VELOCITY_GUARD")
do
  local supported, reason = moonmod.api.supports("physics.velocity_set")
  assert(supported and reason == "")
  local velocity, velocity_failure = game.physics.velocity_set({
    entity = "chronicle-two:monster:0:1", velocity = {x = 1, y = 0, z = 0}
  })
  assert(velocity == nil and velocity_failure.code == "failed")
end
moonmod.log("info", "MOONMOD_HEADLESS_PHYSICS_VELOCITY_SET_GUARD")
do
  local stick_axis = assert(game.input.axis({axis = "left_x"}))
  local trigger_axis = assert(game.input.axis({axis = "right_trigger"}))
  assert(stick_axis.axis == "left_x" and stick_axis.pad == 0 and type(stick_axis.connected) == "boolean")
  assert(stick_axis.value >= -1 and stick_axis.value <= 1)
  assert(trigger_axis.axis == "right_trigger" and type(trigger_axis.connected) == "boolean")
  assert(trigger_axis.value >= 0 and trigger_axis.value <= 1)
end
moonmod.log("info", "MOONMOD_HEADLESS_INPUT_OK")
do
  local camera, camera_failure = game.render.camera_get()
  assert(camera == nil and camera_failure.code == "failed")
end
local profile = assert(game.debug.profile())
assert(profile.lua_memory.used_bytes > 0 and profile.lua_memory.used_bytes <= profile.lua_memory.limit_bytes)
assert(profile.events.subscriptions == 0 and profile.resources.handles == 0 and profile.resources.bytes == 0)
assert(type(profile.storage.keys) == "number" and profile.storage.keys <= 5)
assert(profile.storage.bytes <= profile.storage.limit_bytes)
moonmod.log("info", "MOONMOD_HEADLESS_DEBUG_PROFILE_OK")
local function verify_locations()
  for _, path in ipairs({"locations.list", "locations.get"}) do
    local supported, reason = moonmod.api.supports(path)
    moonmod.log("info", "MOONMOD_LOCATION_SUPPORT|" .. path .. "|" .. tostring(supported) .. "|" .. reason)
  end
  local locations, list_error = game.locations.list()
  if locations == nil then
    moonmod.log("info", "MOONMOD_LOCATION_LIST_ERROR|" .. tostring(list_error.code) .. "|" .. list_error.message)
    return
  end
  moonmod.log("info", "MOONMOD_LOCATION_LIST_COUNT|" .. #locations)
  for index, location in ipairs(locations) do
    moonmod.log("info", "MOONMOD_LOCATION_ENTRY|" .. index .. "|" .. location.id .. "|" .. location.name .. "|" .. location.kind .. "|" .. location.area_id)
  end
  if #locations ~= 3 or locations[1].id ~= "chronicle-two:location:probe-town" or
      locations[1].name ~= "Moon Factory Fixture Town" or locations[1].map_name ~= "probe-town" or
      locations[1].kind ~= "georama" or locations[1].native_map_id ~= 0 or locations[1].area_id ~= 2 or
      locations[2].id ~= "chronicle-two:location:probe-field" or locations[2].kind ~= "interior" or
      locations[2].native_map_id ~= 1 or locations[2].area_id ~= 4 or
      locations[3].id ~= "chronicle-two:location:moonmod.headless-probe:moon-cave" or
      locations[3].map_name ~= "moonprobe" or locations[3].name ~= "Moon Factory Cave" or
      locations[3].kind ~= "interior" or locations[3].native_map_id ~= 2 or
      locations[3].source ~= "mod_package" or locations[3].owner ~= "moonmod.headless-probe" or
      locations[3].status ~= "registered" or locations[3].texture_source_map ~= "probe-field" or
      #locations[3].resources ~= 4 then
    return
  end
  local location, get_error = game.locations.get({id = locations[1].id})
  if location == nil then
    moonmod.log("info", "MOONMOD_LOCATION_GET_ERROR|" .. tostring(get_error.code) .. "|" .. get_error.message)
    return
  end
  if location.native_map_id ~= 0 or location.title ~= "Moon Factory Fixture Town" then return end
  local local_location = game.locations.get({id = "probe-field"})
  if local_location == nil or local_location.id ~= "chronicle-two:location:probe-field" then return end
  local authored_location = game.locations.get({id = locations[3].id})
  if authored_location == nil or authored_location.source ~= "mod_package" or
      authored_location.native_map_id ~= 2 or authored_location.status ~= "registered" then return end
  local runtime_snapshot = assert(game.debug.inspect())
  assert(runtime_snapshot.scene_id == "title" and runtime_snapshot.native_mode == 3)
  assert(runtime_snapshot.native_map_id == nil and runtime_snapshot.native_floor_id == nil)
  moonmod.log("info", "MOONMOD_HEADLESS_DEBUG_INSPECT_OK")
  local queued_transition, transition_error = game.world.transition({id = locations[1].id})
  assert(queued_transition == nil and transition_error.code == "failed")
  assert(string.find(transition_error.message, "active town or dungeon", 1, true) ~= nil)
  local authored_transition, authored_transition_error = game.world.transition({id = locations[3].id})
  assert(authored_transition == nil and authored_transition_error.code == "failed")
  assert(string.find(authored_transition_error.message, "active town or dungeon", 1, true) ~= nil)
  moonmod.log("info", "MOONMOD_HEADLESS_WORLD_TRANSITION_CONTEXT_GUARD")
  moonmod.log("info", "MOONMOD_HEADLESS_MOD_LOCATION_TRANSITION_GUARD")
  moonmod.log("info", "MOONMOD_HEADLESS_LOCATIONS_OK|" .. #locations)
end
game.events.subscribe("game.scene.will_enter", function(scene)
  assert(scene.scene_id == "title" and scene.native_mode == 3)
  verify_locations()
  moonmod.log("info", "MOONMOD_HEADLESS_SCENE_WILL_ENTER:" .. scene.scene_id .. ":" .. tostring(scene.native_mode))
end)
local weapon_list_supported, weapon_list_reason = moonmod.api.supports("weapons.list")
local weapon_get_supported, weapon_get_reason = moonmod.api.supports("weapons.get")
local weapon_equip_supported, weapon_equip_reason = moonmod.api.supports("weapons.equip")
local weapon_upgrade_supported, weapon_upgrade_reason = moonmod.api.supports("weapons.upgrade")
assert(weapon_list_supported and weapon_list_reason == "")
assert(weapon_get_supported and weapon_get_reason == "")
assert(weapon_equip_supported and weapon_equip_reason == "")
assert(weapon_upgrade_supported and weapon_upgrade_reason == "")
assert(moonmod.api.supports("content.list"))
assert(moonmod.api.supports("content.register"))
do
  local content_unregister_supported, content_unregister_reason = moonmod.api.supports("content.unregister")
  assert(content_unregister_supported and content_unregister_reason == "")
end
local weapons = assert(game.weapons.list())
assert(type(weapons) == "table")
local missing_weapon, missing_weapon_error = game.weapons.get({item_no = 80})
assert(missing_weapon == nil and missing_weapon_error.code == "failed")
local missing_equip, missing_equip_error = game.weapons.equip({id = "chronicle-two:weapon:80"})
assert(missing_equip == nil and missing_equip_error.code == "failed")
local missing_upgrade, missing_upgrade_error = game.weapons.upgrade({id = "chronicle-two:weapon:80"})
assert(missing_upgrade == nil and missing_upgrade_error.code == "failed")
moonmod.log("info", "MOONMOD_HEADLESS_WEAPON_EQUIP_EMPTY_OK")
moonmod.log("info", "MOONMOD_HEADLESS_WEAPON_UPGRADE_EMPTY_OK")
moonmod.log("info", "MOONMOD_HEADLESS_WEAPONS_EMPTY_OK")
local registered_weapon, weapon_register_error = game.content.register({
  type = "weapon",
  definition = {id = "headless-blade", name = "Headless Blade", template_weapon = 1,
    stats = {attack = 12}, stat_caps = {attack = 120}, assets = {}}
})
assert(registered_weapon, weapon_register_error and weapon_register_error.message)
assert(registered_weapon.id == "moonmod.headless-probe:headless-blade")
assert(registered_weapon.status == "pending")
local pending_weapon, pending_weapon_error = game.weapons.get(registered_weapon.id)
assert(pending_weapon == nil and pending_weapon_error.code == "phase_violation")
moonmod.log("info", "MOONMOD_HEADLESS_WEAPON_PENDING:" .. registered_weapon.id)
do
  local content_listing = assert(game.content.list())
  assert(content_listing.count >= 1 and #content_listing.entries == content_listing.count)
  local listed_headless_weapon = false
  for _, entry in ipairs(content_listing.entries) do
    if entry.id == registered_weapon.id and entry.content_type == "weapon" then listed_headless_weapon = true end
  end
  assert(listed_headless_weapon)
  moonmod.log("info", "MOONMOD_HEADLESS_CONTENT_REGISTRY_OK|" .. content_listing.count)
end
do
  local disposable_weapon = assert(game.content.register({
    type = "weapon",
    definition = {id = "discarded-blade", name = "Discarded Blade", template_weapon = 1, assets = {}}
  }))
  assert(disposable_weapon.status == "pending")
  local removed_pending_weapon = assert(game.content.unregister({type = "weapon", id = disposable_weapon.id}))
  assert(removed_pending_weapon.removed and removed_pending_weapon.type == "weapon" and
         removed_pending_weapon.id == disposable_weapon.id)
  local content_after_unregister = assert(game.content.list())
  for _, entry in ipairs(content_after_unregister.entries) do
    assert(entry.id ~= disposable_weapon.id)
  end
end
moonmod.log("info", "MOONMOD_HEADLESS_CONTENT_UNREGISTER_OK")
local supported, reason = moonmod.api.supports("items.register")
assert(supported and reason == "")
local registered_text, localization_error = game.localization.register({
  id = "headless.greeting", translations = {en = "Hello", fr = "Bonjour"}
})
assert(localization_error == nil and registered_text.id == "moonmod.headless-probe:headless.greeting")
assert(game.localization.get({id = "headless.greeting", language = "fr-CA"}) == "Bonjour")
assert(game.localization.get({id = "headless.greeting", language = "ja"}) == "Hello")
moonmod.log("info", "MOONMOD_HEADLESS_LOCALIZATION_OK")
local item, failure = game.content.register({
  type = "item",
  definition = {id = "headless-probe", name = "Probe Tonic", kind = "consumable", template_item = 274,
    max_count = 10, max_stack = 5, effect = {kind = "heal", amount = 12}, assets = {}}
})
assert(failure == nil and item.id == "moonmod.headless-probe:headless-probe" and item.status == "pending")
moonmod.log("info", "MOONMOD_HEADLESS_ITEM_PENDING:" .. item.id)
local listed_items = assert(game.items.list())
local found_pending_item = false
for _, listed_item in ipairs(listed_items) do
  if listed_item.id == item.id and listed_item.status == "pending" then found_pending_item = true end
end
assert(found_pending_item)
local pending_item, pending_item_error = game.items.get(item.id)
assert(pending_item and pending_item.status == "pending" and pending_item_error == nil)
local missing_builtin, missing_builtin_error = game.items.get("chronicle-two:item:274")
assert(missing_builtin == nil and missing_builtin_error.code == "failed")
local use_supported, use_reason = moonmod.api.supports("items.use")
assert(use_supported and use_reason == "")
local queued_use, queued_use_error = game.items.use({id = item.id, target = "max"})
assert(queued_use == nil and queued_use_error.code == "phase_violation")
moonmod.log("info", "MOONMOD_HEADLESS_ITEMS_PENDING_LISTED")
local dungeon_supported, dungeon_reason = moonmod.api.supports("dungeons.register")
assert(dungeon_supported and dungeon_reason == "")
local player_supported, player_reason = moonmod.api.supports("player.get")
assert(player_supported and player_reason == "")
local player_set_supported, player_set_reason = moonmod.api.supports("player.set")
assert(player_set_supported and player_set_reason == "")
local player_set_without_save, player_set_without_save_error = game.player.set({hp = 1})
assert(player_set_without_save == nil and player_set_without_save_error.code == "failed")
moonmod.log("info", "MOONMOD_HEADLESS_PLAYER_SET_NO_SAVE")
local status_add_supported, status_add_reason = moonmod.api.supports("player.status_add")
assert(status_add_supported and status_add_reason == "")
local status_remove_supported, status_remove_reason = moonmod.api.supports("player.status_remove")
assert(status_remove_supported and status_remove_reason == "")
local status_add_without_save, status_add_without_save_error = game.player.status_add({status = "power", duration_frames = 60})
assert(status_add_without_save == nil and status_add_without_save_error.code == "failed")
local status_remove_without_save, status_remove_without_save_error = game.player.status_remove({status = "power"})
assert(status_remove_without_save == nil and status_remove_without_save_error.code == "failed")
moonmod.log("info", "MOONMOD_HEADLESS_STATUS_NO_SAVE")
local set_active_supported, set_active_reason = moonmod.api.supports("party.set_active")
assert(set_active_supported and set_active_reason == "")
local set_active_without_save, set_active_without_save_error = game.party.set_active({id = "max"})
assert(set_active_without_save == nil and set_active_without_save_error.code == "failed")
moonmod.log("info", "MOONMOD_HEADLESS_PARTY_SET_ACTIVE_NO_SAVE")
local recruit_supported, recruit_reason = moonmod.api.supports("party.recruit")
assert(recruit_supported and recruit_reason == "")
local recruit_without_save, recruit_without_save_error = game.party.recruit({npc_id = 7})
assert(recruit_without_save == nil and recruit_without_save_error.code == "failed")
moonmod.log("info", "MOONMOD_HEADLESS_PARTY_RECRUIT_NO_SAVE")
local party_supported, party_reason = moonmod.api.supports("party.list")
assert(party_supported and party_reason == "")
local characters_list_supported, characters_list_reason = moonmod.api.supports("characters.list")
assert(characters_list_supported and characters_list_reason == "")
local characters_get_supported, characters_get_reason = moonmod.api.supports("characters.get")
assert(characters_get_supported and characters_get_reason == "")
local characters_register_supported, characters_register_reason = moonmod.api.supports("characters.register")
assert(characters_register_supported and characters_register_reason == "")
local characters_spawn_supported, characters_spawn_reason = moonmod.api.supports("characters.spawn")
assert(characters_spawn_supported and characters_spawn_reason == "")
local headless_enemy = assert(game.content.register({
  type = "character",
  definition = {id = "headless-sentinel", kind = "enemy", name = "Headless Sentinel", template_monster_id = 0,
    stats = {hp = 300, attack = 75, defense = 41}, assets = {}}
}))
assert(headless_enemy.status == "registered" and headless_enemy.kind == "enemy")
assert(headless_enemy.model == "e201a" and headless_enemy.script == "e201a")
local loaded_enemy = assert(game.characters.get(headless_enemy.id))
assert(loaded_enemy.native_monster_id == headless_enemy.native_monster_id and loaded_enemy.stats.hp == 300)
moonmod.log("info", "MOONMOD_HEADLESS_CHARACTER_REGISTERED:" .. headless_enemy.id)
local spawned_enemy, spawn_error = game.characters.spawn({
  character = headless_enemy.id, position = {x = 12, y = 0, z = -8}
})
assert(spawned_enemy == nil and spawn_error.code == "failed")
moonmod.log("info", "MOONMOD_HEADLESS_CHARACTER_SPAWN_NO_DUNGEON")
local economy_get_supported, economy_get_reason = moonmod.api.supports("economy.get")
assert(economy_get_supported and economy_get_reason == "")
local economy_add_supported, economy_add_reason = moonmod.api.supports("economy.add")
assert(economy_add_supported and economy_add_reason == "")
local economy_spend_supported, economy_spend_reason = moonmod.api.supports("economy.spend")
assert(economy_spend_supported and economy_spend_reason == "")
local player_damage_supported, player_damage_reason = moonmod.api.supports("player.damage")
assert(player_damage_supported and player_damage_reason == "")
local item_remove_supported, item_remove_reason = moonmod.api.supports("items.remove")
assert(item_remove_supported and item_remove_reason == "")
local world_supported, world_reason = moonmod.api.supports("world.get")
assert(world_supported and world_reason == "")
local world, world_failure = game.world.get()
if world == nil then
  assert(world_failure.code == "failed")
  moonmod.log("info", "MOONMOD_HEADLESS_WORLD_QUERY_NO_SAVE")
else
  assert(type(world.context) == "string" and type(world.map_id) == "number")
  moonmod.log("info", "MOONMOD_HEADLESS_WORLD_QUERY_STATE:" .. world.context .. "|" .. tostring(world.map_id))
end
moonmod.log("info", "MOONMOD_HEADLESS_WORLD_QUERY_OK")
local world_time_supported, world_time_reason = moonmod.api.supports("world.set_time")
assert(world_time_supported and world_time_reason == "")
local world_time, world_time_error = game.world.set_time({fraction = 0.5})
assert(world_time == nil and world_time_error.code == "failed")
moonmod.log("info", "MOONMOD_HEADLESS_WORLD_TIME_NO_SAVE")
for _, path in ipairs({"world.story_flag_get", "world.story_flag_set", "world.story_value_get",
                       "world.story_value_set", "world.map_flag_get", "world.map_flag_set"}) do
  local supported, reason = moonmod.api.supports(path)
  assert(supported and reason == "", path)
end
local story_flag, story_flag_error = game.world.story_flag_get({id = 0})
assert(story_flag == nil and story_flag_error.code == "failed")
local story_flag_write, story_flag_write_error = game.world.story_flag_set({id = 0, value = true})
assert(story_flag_write == nil and story_flag_write_error.code == "failed")
local story_value, story_value_error = game.world.story_value_get({id = 0})
assert(story_value == nil and story_value_error.code == "failed")
local story_value_write, story_value_write_error = game.world.story_value_set({id = 0, value = 1})
assert(story_value_write == nil and story_value_write_error.code == "failed")
local map_flag, map_flag_error = game.world.map_flag_get({map_id = 0, flag_id = 0})
assert(map_flag == nil and map_flag_error.code == "failed")
local map_flag_write, map_flag_write_error = game.world.map_flag_set({map_id = 0, flag_id = 0, value = true})
assert(map_flag_write == nil and map_flag_write_error.code == "failed")
moonmod.log("info", "MOONMOD_HEADLESS_WORLD_FLAGS_NO_SAVE")
local combat_supported, combat_reason = moonmod.api.supports("combat.get")
assert(combat_supported and combat_reason == "")
local combat, combat_error = game.combat.get()
assert(combat == nil and combat_error.code == "failed")
local combat_heal_supported, combat_heal_reason = moonmod.api.supports("combat.heal")
assert(combat_heal_supported and combat_heal_reason == "")
local combat_heal, combat_heal_error = game.combat.heal({entity = "chronicle-two:monster:0:1", amount = 1})
assert(combat_heal == nil and combat_heal_error.code == "failed")
do
  local combat_damage_supported, combat_damage_reason = moonmod.api.supports("combat.damage")
  assert(combat_damage_supported and combat_damage_reason == "")
  local combat_damage, combat_damage_error = game.combat.damage({entity = "chronicle-two:monster:0:1", amount = 1})
  assert(combat_damage == nil and combat_damage_error.code == "failed")
end
moonmod.log("info", "MOONMOD_HEADLESS_COMBAT_DAMAGE_NO_DUNGEON")
moonmod.log("info", "MOONMOD_HEADLESS_COMBAT_NO_DUNGEON")
local entities_supported, entities_reason = moonmod.api.supports("entities.query")
assert(entities_supported and entities_reason == "")
local entity_get_supported, entity_get_reason = moonmod.api.supports("entities.get")
assert(entity_get_supported and entity_get_reason == "")
local entity_spawn_supported, entity_spawn_reason = moonmod.api.supports("entities.spawn")
assert(entity_spawn_supported and entity_spawn_reason == "")
do
  local entity_despawn_supported, entity_despawn_reason = moonmod.api.supports("entities.despawn")
  assert(entity_despawn_supported and entity_despawn_reason == "")
end
local entities, entities_error = game.entities.query({type = "monster"})
assert(entities == nil and entities_error.code == "failed")
moonmod.log("info", "MOONMOD_HEADLESS_ENTITIES_NO_DUNGEON")
local entity_spawn, entity_spawn_error = game.entities.spawn({
  character = headless_enemy.id, position = {x = 12, y = 0, z = -8}
})
assert(entity_spawn == nil and entity_spawn_error.code == "failed")
moonmod.log("info", "MOONMOD_HEADLESS_ENTITY_SPAWN_NO_DUNGEON")
do
  local entity_despawn, entity_despawn_error = game.entities.despawn({id = headless_enemy.id})
  assert(entity_despawn == nil and entity_despawn_error.code == "failed")
  assert(string.find(entity_despawn_error.message, "created by characters.spawn", 1, true) ~= nil)
end
moonmod.log("info", "MOONMOD_HEADLESS_ENTITY_DESPAWN_OWNERSHIP_OK")
local dungeon, dungeon_failure = game.content.register({
  type = "dungeon",
  definition = {id = "headless-vault", name = "Headless Vault", dungeon = 0,
    floors = {{id = "annex", source_floor = 1, flags = {"exit"}, seal = "max"}}}
})
assert(dungeon_failure == nil and dungeon.id == "moonmod.headless-probe:headless-vault")
assert(dungeon.floors[1].status == "pending")
assert(dungeon.floors[1].flags[1] == "exit" and dungeon.floors[1].seal == "max")
local dungeons = assert(game.dungeons.list())
assert(#dungeons == 1 and dungeons[1].id == dungeon.id and dungeons[1].floors[1].status == "pending")
local dungeon_read = assert(game.dungeons.get(dungeon.id))
assert(dungeon_read.id == dungeon.id and dungeon_read.floors[1].flags[1] == "exit")
moonmod.log("info", "MOONMOD_HEADLESS_DUNGEON_PENDING:" .. dungeon.id)
do
  local content_snapshot = assert(game.content.list())
  local types = {}
  for _, entry in ipairs(content_snapshot.entries) do types[entry.content_type] = true end
  assert(content_snapshot.count >= 5 and #content_snapshot.entries == content_snapshot.count)
  assert(types.item and types.weapon and types.character and types.dungeon and types.georama_piece)
  moonmod.log("info", "MOONMOD_HEADLESS_CONTENT_ALL_TYPES_OK|" .. content_snapshot.count)
end
local quest_definition = {
  id = "headless-story", title = "Headless Story", description = "Two-launch quest probe",
  objectives = {{id = "find", title = "Find two clues", target = 2},
                {id = "return", title = "Return to town", target = 1}}
}
local saved_quest, saved_quest_error = game.quests.get({id = "headless-story"})
if saved_quest == nil then
  assert(saved_quest_error.code == "failed")
  local started_events, advanced_events, completed_events = 0, 0, 0
  game.events.subscribe("quests.started", function(event)
    assert(event.id == "moonmod.headless-probe:headless-story")
    started_events = started_events + 1
  end)
  game.events.subscribe("quests.objective_advanced", function(event)
    assert(event.id == "moonmod.headless-probe:headless-story")
    advanced_events = advanced_events + 1
  end)
  game.events.subscribe("quests.completed", function(event)
    assert(event.id == "moonmod.headless-probe:headless-story")
    completed_events = completed_events + 1
  end)
  local registered_quest = assert(game.quests.register(quest_definition))
  assert(registered_quest.id == "moonmod.headless-probe:headless-story" and not registered_quest.idempotent)
  assert(assert(game.quests.register(quest_definition)).idempotent)
  assert(assert(game.quests.start({id = "headless-story"})).status == "active")
  assert(assert(game.quests.advance({id = "headless-story", objective = "find", amount = 2})).progress == 2)
  assert(assert(game.quests.advance({id = "headless-story", objective = "return"})).status == "completed")
  assert(started_events == 1 and advanced_events == 2 and completed_events == 1)
  local completed_quest = assert(game.quests.get({id = "headless-story"}))
  assert(completed_quest.status == "completed" and completed_quest.objectives[1].progress == 2)
  moonmod.log("info", "MOONMOD_HEADLESS_QUEST_LIFECYCLE_OK")
else
  assert(saved_quest.status == "completed" and saved_quest.objectives[2].progress == 1)
  assert(assert(game.quests.register(quest_definition)).idempotent)
  moonmod.log("info", "MOONMOD_HEADLESS_QUEST_PERSISTED")
end
local all_quests = assert(game.quests.get())
assert(#all_quests.quests == 1)
local internal_quest_save, internal_quest_save_error = game.save.get("__moonmod_quests_v1")
assert(internal_quest_save == nil and internal_quest_save_error.code == "invalid_argument")
assert(moonmod.storage.get("__moonmod_quests_v1") == nil)
moonmod.log("info", "MOONMOD_HEADLESS_QUESTS_OK")
local prior_storage = moonmod.storage.get("headless-restart")
if prior_storage == nil then
  assert(moonmod.storage.set("headless-restart", "persisted"))
  moonmod.log("info", "MOONMOD_HEADLESS_STORAGE_INITIALIZED")
else
  assert(prior_storage == "persisted")
  moonmod.log("info", "MOONMOD_HEADLESS_STORAGE_RELOADED")
end
local save_version = assert(game.save.get("__moonmod_schema_version"))
assert(save_version == 0 or save_version == 1)
local migration_outside_load, migration_phase_error = game.save.migrate({from_version = 0, to_version = 1})
assert(migration_outside_load == nil and migration_phase_error.code == "phase_violation")
local reserved_version_write, reserved_version_error = game.save.set("__moonmod_schema_version", 9)
assert(reserved_version_write == nil and reserved_version_error.code == "invalid_argument")
local storage_version_write, storage_version_write_error = moonmod.storage.set("__moonmod_schema_version", 9)
assert(storage_version_write == nil and storage_version_write_error:match("reserved"))
local storage_version_delete, storage_version_delete_error = moonmod.storage.delete("__moonmod_schema_version")
assert(storage_version_delete == nil and storage_version_delete_error:match("reserved"))
if save_version == 0 then
  assert(game.save.set("legacy", {coins = 8}))
else
  assert(game.save.get("migration-probe").revision == 1)
end
local mod = {}
function mod.on_load()
  local version = assert(game.save.get("__moonmod_schema_version"))
  if version == 0 then
    local migration = assert(game.save.migrate({
      from_version = 0,
      to_version = 1,
      set = { ["migration-probe"] = {revision = 1, migrated = true} },
      delete = {"legacy"}
    }))
    assert(migration.migrated and migration.to_version == 1)
    moonmod.log("info", "MOONMOD_HEADLESS_SAVE_MIGRATED")
  else
    assert(version == 1)
    local migration = assert(game.save.migrate({
      from_version = 0,
      to_version = 1,
      set = { ["migration-probe"] = {revision = 1, migrated = true} },
      delete = {"legacy"}
    }))
    assert(not migration.migrated and migration.version == 1)
    moonmod.log("info", "MOONMOD_HEADLESS_SAVE_MIGRATION_RELOADED")
  end
end
assert(assert(game.info.get()).api_major == 1)
moonmod.log("info", "MOONMOD_HEADLESS_API_OK")
return mod
]=])
file(WRITE "$ENV{DC2_SAVE}/moonmod/profile.json" [=[
{"format_version":1,"enabled":{"moonmod.headless-probe":true},"settings":{}}
]=])
file(REMOVE "$ENV{DC2_SAVE}/moonmod-smoke.png")
execute_process(COMMAND "${EXE}" --headless --offscreen --frames 3 --screenshot "$ENV{DC2_SAVE}/moonmod-smoke.png"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
set(first_log "${out}${err}")
set(second_log "")
set(second_rc 1)
if(rc EQUAL 0)
    execute_process(COMMAND "${EXE}" --headless --offscreen --frames 3 --screenshot "$ENV{DC2_SAVE}/moonmod-smoke.png"
        RESULT_VARIABLE second_rc OUTPUT_VARIABLE out_second ERROR_VARIABLE err_second TIMEOUT 120)
    set(second_log "${out_second}${err_second}")
endif()
set(log "${first_log}${second_log}")
message("${log}")
file(WRITE "$ENV{DC2_SAVE}/moonmod-api-report.txt" "${log}")
set(png "$ENV{DC2_SAVE}/moonmod-smoke.png")
set(sig "")
if(EXISTS "${png}")
    file(READ "${png}" sig LIMIT 8 HEX)
endif()
string(FIND "${log}" "dataread: indexed " index_at)
string(FIND "${log}" "mglib: frame 3 written to" frame_at)
string(FIND "${log}" "moonmod[error]" api_error_at)
string(FIND "${log}" "event 'game.tick' failed" callback_error_at)
string(FIND "${log}" "MOONMOD_HEADLESS_API_OK" moonmod_at)
string(FIND "${log}" "MOONMOD_API_STATUS|items.register|supported" supported_item_at)
string(FIND "${log}" "MOONMOD_API_STATUS|items.list|supported" supported_items_list_at)
string(FIND "${log}" "MOONMOD_API_STATUS|items.get|supported" supported_items_get_at)
string(FIND "${log}" "MOONMOD_API_STATUS|items.use|supported" supported_items_use_at)
string(FIND "${log}" "MOONMOD_API_STATUS|localization.register|supported" supported_localization_register_at)
string(FIND "${log}" "MOONMOD_API_STATUS|localization.get|supported" supported_localization_get_at)
string(FIND "${log}" "MOONMOD_HEADLESS_LOCALIZATION_OK" localization_at)
string(FIND "${log}" "MOONMOD_API_STATUS|input.bind|supported" supported_input_bind_at)
string(FIND "${log}" "MOONMOD_API_STATUS|input.state|supported" supported_input_state_at)
string(FIND "${log}" "MOONMOD_API_STATUS|input.consume|supported" supported_input_consume_at)
string(FIND "${log}" "MOONMOD_API_STATUS|input.unbind|supported" supported_input_unbind_at)
string(FIND "${log}" "MOONMOD_API_STATUS|input.context_push|supported" supported_input_context_push_at)
string(FIND "${log}" "MOONMOD_API_STATUS|input.context_pop|supported" supported_input_context_pop_at)
string(FIND "${log}" "MOONMOD_API_STATUS|input.context_get|supported" supported_input_context_get_at)
string(FIND "${log}" "MOONMOD_HEADLESS_INPUT_OK" input_ok_at)
string(FIND "${log}" "MOONMOD_HEADLESS_INPUT_CONTEXTS_OK" input_contexts_at)
string(FIND "${log}" "MOONMOD_API_STATUS|physics.velocity_get|supported" supported_physics_velocity_at)
string(FIND "${log}" "MOONMOD_HEADLESS_PHYSICS_VELOCITY_GUARD" physics_velocity_guard_at)
string(FIND "${log}" "MOONMOD_API_STATUS|physics.velocity_set|supported" supported_physics_velocity_set_at)
string(FIND "${log}" "MOONMOD_HEADLESS_PHYSICS_VELOCITY_SET_GUARD" physics_velocity_set_guard_at)
string(FIND "${log}" "MOONMOD_API_STATUS|resources.load|supported" supported_resources_load_at)
string(FIND "${log}" "MOONMOD_API_STATUS|resources.resolve|supported" supported_resources_resolve_at)
string(FIND "${log}" "MOONMOD_API_STATUS|resources.release|supported" supported_resources_release_at)
string(FIND "${log}" "MOONMOD_HEADLESS_RESOURCES_OK" resources_ok_at)
string(FIND "${log}" "MOONMOD_API_STATUS|ui.notify|supported" supported_ui_notify_at)
string(FIND "${log}" "MOONMOD_HEADLESS_UI_NOTIFY_OK" ui_notify_ok_at)
string(FIND "${log}" "MOONMOD_API_STATUS|ui.register|supported" supported_ui_register_at)
string(FIND "${log}" "MOONMOD_API_STATUS|ui.remove|supported" supported_ui_remove_at)
string(FIND "${log}" "MOONMOD_HEADLESS_UI_ACTIONS_OK" ui_actions_ok_at)
string(FIND "${log}" "MOONMOD_API_STATUS|debug.profile|supported" supported_debug_profile_at)
string(FIND "${log}" "MOONMOD_HEADLESS_DEBUG_PROFILE_OK" debug_profile_ok_at)
string(FIND "${log}" "MOONMOD_API_STATUS|weapons.list|supported" supported_weapons_list_at)
string(FIND "${log}" "MOONMOD_API_STATUS|weapons.get|supported" supported_weapons_get_at)
string(FIND "${log}" "MOONMOD_API_STATUS|weapons.equip|supported" supported_weapons_equip_at)
string(FIND "${log}" "MOONMOD_API_STATUS|weapons.upgrade|supported" supported_weapons_upgrade_at)
string(FIND "${log}" "MOONMOD_API_STATUS|weapons.register|supported" supported_weapons_register_at)
string(FIND "${log}" "MOONMOD_HEADLESS_WEAPONS_EMPTY_OK" weapons_empty_at)
string(FIND "${log}" "MOONMOD_HEADLESS_WEAPON_EQUIP_EMPTY_OK" weapons_equip_empty_at)
string(FIND "${log}" "MOONMOD_HEADLESS_WEAPON_UPGRADE_EMPTY_OK" weapons_upgrade_empty_at)
string(FIND "${log}" "MOONMOD_HEADLESS_WEAPON_PENDING:moonmod.headless-probe:headless-blade" weapon_pending_at)
string(FIND "${log}" "MOONMOD_HEADLESS_CONTENT_REGISTRY_OK|" content_registry_at)
string(FIND "${log}" "MOONMOD_HEADLESS_CONTENT_UNREGISTER_OK" content_unregister_at)
string(FIND "${log}" "MOONMOD_HEADLESS_CONTENT_ALL_TYPES_OK|" content_all_types_at)
string(FIND "${log}" "MOONMOD_HEADLESS_ITEM_PENDING:moonmod.headless-probe:headless-probe" item_pending_at)
string(FIND "${log}" "MOONMOD_HEADLESS_ITEMS_PENDING_LISTED" pending_items_listed_at)
string(FIND "${log}" "MOONMOD_API_STATUS|world.register_dungeon|supported" supported_dungeon_at)
string(FIND "${log}" "MOONMOD_API_STATUS|dungeons.register|supported" supported_dungeons_register_at)
string(FIND "${log}" "MOONMOD_API_STATUS|dungeons.list|supported" supported_dungeons_list_at)
string(FIND "${log}" "MOONMOD_API_STATUS|dungeons.get|supported" supported_dungeons_get_at)
string(FIND "${log}" "MOONMOD_API_STATUS|locations.list|supported" supported_locations_list_at)
string(FIND "${log}" "MOONMOD_API_STATUS|locations.get|supported" supported_locations_get_at)
string(FIND "${log}" "MOONMOD_API_STATUS|locations.register|supported" supported_locations_register_at)
string(FIND "${log}" "MOONMOD_HEADLESS_LOCATION_REGISTERED|chronicle-two:location:moonmod.headless-probe:moon-cave" location_registered_at)
string(FIND "${log}" "MOONMOD_HEADLESS_LOCATIONS_OK|3" locations_ok_at)
string(FIND "${log}" "MOONMOD_API_STATUS|models.resolve|supported" supported_model_resolve_at)
string(FIND "${log}" "MOONMOD_API_STATUS|models.register|supported" supported_model_register_at)
string(FIND "${log}" "MOONMOD_API_STATUS|models.unregister|supported" supported_model_unregister_at)
string(FIND "${log}" "MOONMOD_HEADLESS_MODEL_RESOLVE_OK" model_resolve_at)
string(FIND "${log}" "MOONMOD_HEADLESS_MODEL_UNREGISTER_OK" model_unregister_at)
string(FIND "${log}" "MOONMOD_HEADLESS_MODEL_REGISTER_OK|moonmod.headless-probe:headless-model" model_register_at)
string(FIND "${log}" "MOONMOD_API_STATUS|animations.register|supported" supported_animations_register_at)
string(FIND "${log}" "MOONMOD_API_STATUS|animations.play|supported" supported_animations_play_at)
string(FIND "${log}" "MOONMOD_API_STATUS|animations.stop|supported" supported_animations_stop_at)
string(FIND "${log}" "MOONMOD_HEADLESS_ANIMATIONS_NO_DUNGEON_OK|moonmod.headless-probe:headless-idle" animations_no_dungeon_at)
string(FIND "${log}" "MOONMOD_API_STATUS|player.get|supported" supported_player_at)
string(FIND "${log}" "MOONMOD_API_STATUS|player.set|supported" supported_player_set_at)
string(FIND "${log}" "MOONMOD_HEADLESS_PLAYER_SET_NO_SAVE" player_set_no_save_at)
string(FIND "${log}" "MOONMOD_HEADLESS_FUTURE_DOMAINS_UNSUPPORTED" future_domains_unsupported_at)
string(FIND "${log}" "MOONMOD_HEADLESS_SKILLS_OK" skills_ok_at)
string(FIND "${log}" "MOONMOD_API_STATUS|scripts.register_function|supported" supported_script_register_at)
string(FIND "${log}" "MOONMOD_API_STATUS|scripts.list_functions|supported" supported_script_list_at)
string(FIND "${log}" "MOONMOD_API_STATUS|scripts.call_function|supported" supported_script_call_at)
string(FIND "${log}" "MOONMOD_API_STATUS|scripts.unregister_function|supported" supported_script_unregister_at)
string(FIND "${log}" "MOONMOD_HEADLESS_SCRIPT_FUNCTIONS_OK" script_functions_ok_at)
string(FIND "${log}" "MOONMOD_API_STATUS|player.damage|supported" supported_player_damage_at)
string(FIND "${log}" "MOONMOD_API_STATUS|player.status_add|supported" supported_player_status_add_at)
string(FIND "${log}" "MOONMOD_API_STATUS|player.status_remove|supported" supported_player_status_remove_at)
string(FIND "${log}" "MOONMOD_HEADLESS_STATUS_NO_SAVE" player_status_no_save_at)
string(FIND "${log}" "MOONMOD_API_STATUS|party.set_active|supported" supported_party_set_active_at)
string(FIND "${log}" "MOONMOD_HEADLESS_PARTY_SET_ACTIVE_NO_SAVE" party_set_active_no_save_at)
string(FIND "${log}" "MOONMOD_API_STATUS|party.recruit|supported" supported_party_recruit_at)
string(FIND "${log}" "MOONMOD_HEADLESS_PARTY_RECRUIT_NO_SAVE" party_recruit_no_save_at)
string(FIND "${log}" "MOONMOD_API_STATUS|party.list|supported" supported_party_at)
string(FIND "${log}" "MOONMOD_API_STATUS|characters.list|supported" supported_characters_list_at)
string(FIND "${log}" "MOONMOD_API_STATUS|characters.get|supported" supported_characters_get_at)
string(FIND "${log}" "MOONMOD_API_STATUS|characters.register|supported" supported_characters_register_at)
string(FIND "${log}" "MOONMOD_API_STATUS|characters.spawn|supported" supported_characters_spawn_at)
string(FIND "${log}" "MOONMOD_HEADLESS_CHARACTER_REGISTERED:moonmod.headless-probe:headless-sentinel" characters_registered_at)
string(FIND "${log}" "MOONMOD_HEADLESS_CHARACTER_SPAWN_NO_DUNGEON" character_spawn_no_dungeon_at)
string(FIND "${log}" "MOONMOD_API_STATUS|economy.get|supported" supported_economy_get_at)
string(FIND "${log}" "MOONMOD_API_STATUS|economy.add|supported" supported_economy_add_at)
string(FIND "${log}" "MOONMOD_API_STATUS|economy.spend|supported" supported_economy_spend_at)
string(FIND "${log}" "MOONMOD_API_STATUS|items.remove|supported" supported_item_remove_at)
string(FIND "${log}" "MOONMOD_API_STATUS|save.migrate|supported" supported_save_migrate_at)
string(FIND "${log}" "MOONMOD_API_STATUS|world.get|supported" supported_world_at)
string(FIND "${log}" "MOONMOD_HEADLESS_WORLD_QUERY_OK" world_query_at)
string(FIND "${log}" "MOONMOD_API_STATUS|world.transition|supported" supported_world_transition_at)
string(FIND "${log}" "MOONMOD_HEADLESS_WORLD_TRANSITION_CONTEXT_GUARD" world_transition_guard_at)
string(FIND "${log}" "MOONMOD_API_STATUS|debug.inspect|supported" supported_debug_inspect_at)
string(FIND "${log}" "MOONMOD_HEADLESS_DEBUG_INSPECT_OK" debug_inspect_at)
string(FIND "${log}" "MOONMOD_API_STATUS|world.set_time|supported" supported_world_time_at)
string(FIND "${log}" "MOONMOD_HEADLESS_WORLD_TIME_NO_SAVE" world_time_no_save_at)
string(FIND "${log}" "MOONMOD_API_STATUS|world.story_flag_get|supported" supported_world_story_flag_get_at)
string(FIND "${log}" "MOONMOD_API_STATUS|world.story_flag_set|supported" supported_world_story_flag_set_at)
string(FIND "${log}" "MOONMOD_API_STATUS|world.story_value_get|supported" supported_world_story_value_get_at)
string(FIND "${log}" "MOONMOD_API_STATUS|world.story_value_set|supported" supported_world_story_value_set_at)
string(FIND "${log}" "MOONMOD_API_STATUS|world.map_flag_get|supported" supported_world_map_flag_get_at)
string(FIND "${log}" "MOONMOD_API_STATUS|world.map_flag_set|supported" supported_world_map_flag_set_at)
string(FIND "${log}" "MOONMOD_HEADLESS_WORLD_FLAGS_NO_SAVE" world_flags_no_save_at)
string(FIND "${log}" "MOONMOD_API_STATUS|combat.get|supported" supported_combat_at)
string(FIND "${log}" "MOONMOD_API_STATUS|combat.heal|supported" supported_combat_heal_at)
string(FIND "${log}" "MOONMOD_API_STATUS|combat.damage|supported" supported_combat_damage_at)
string(FIND "${log}" "MOONMOD_HEADLESS_COMBAT_DAMAGE_NO_DUNGEON" combat_damage_no_dungeon_at)
string(FIND "${log}" "MOONMOD_HEADLESS_COMBAT_NO_DUNGEON" combat_no_dungeon_at)
string(FIND "${log}" "MOONMOD_API_STATUS|entities.query|supported" supported_entities_query_at)
string(FIND "${log}" "MOONMOD_API_STATUS|entities.get|supported" supported_entities_get_at)
string(FIND "${log}" "MOONMOD_API_STATUS|entities.spawn|supported" supported_entities_spawn_at)
string(FIND "${log}" "MOONMOD_API_STATUS|entities.despawn|supported" supported_entities_despawn_at)
string(FIND "${log}" "MOONMOD_HEADLESS_ENTITIES_NO_DUNGEON" entities_no_dungeon_at)
string(FIND "${log}" "MOONMOD_HEADLESS_ENTITY_SPAWN_NO_DUNGEON" entity_spawn_no_dungeon_at)
string(FIND "${log}" "MOONMOD_HEADLESS_ENTITY_DESPAWN_OWNERSHIP_OK" entity_despawn_ownership_at)
string(FIND "${log}" "MOONMOD_API_STATUS|quests.get|supported" supported_quests_get_at)
string(FIND "${log}" "MOONMOD_API_STATUS|quests.register|supported" supported_quests_register_at)
string(FIND "${log}" "MOONMOD_API_STATUS|quests.start|supported" supported_quests_start_at)
string(FIND "${log}" "MOONMOD_API_STATUS|quests.advance|supported" supported_quests_advance_at)
string(FIND "${log}" "MOONMOD_HEADLESS_QUEST_CAPABILITIES_OK" quests_capabilities_at)
string(FIND "${log}" "MOONMOD_HEADLESS_QUEST_LIFECYCLE_OK" quests_lifecycle_at)
string(FIND "${log}" "MOONMOD_HEADLESS_QUEST_PERSISTED" quests_persisted_at)
string(FIND "${log}" "MOONMOD_HEADLESS_QUESTS_OK" quests_ok_at)
string(FIND "${log}" "MOONMOD_HEADLESS_DUNGEON_PENDING:moonmod.headless-probe:headless-vault" dungeon_pending_at)
string(FIND "${log}" "MOONMOD_HEADLESS_STORAGE_INITIALIZED" storage_initial_at)
string(FIND "${log}" "MOONMOD_HEADLESS_STORAGE_RELOADED" storage_reload_at)
string(FIND "${log}" "MOONMOD_HEADLESS_SAVE_MIGRATED" save_migrated_at)
string(FIND "${log}" "MOONMOD_HEADLESS_SAVE_MIGRATION_RELOADED" save_migration_reloaded_at)
foreach(run_number RANGE 1 2)
    if(run_number EQUAL 1)
        set(run_log "${first_log}")
    else()
        set(run_log "${second_log}")
    endif()
    string(FIND "${run_log}" "MOONMOD_HEADLESS_API_OK" run_api_ok_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_ITEM_PENDING:moonmod.headless-probe:headless-probe" run_item_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_DUNGEON_PENDING:moonmod.headless-probe:headless-vault" run_dungeon_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_SCENE_WILL_ENTER:title:3" run_scene_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_COMBAT_NO_DUNGEON" run_combat_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_COMBAT_DAMAGE_NO_DUNGEON" run_combat_damage_at)
    string(FIND "${run_log}" "MOONMOD_API_STATUS|physics.velocity_set|supported" run_physics_velocity_set_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_PHYSICS_VELOCITY_SET_GUARD" run_physics_velocity_set_guard_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_WORLD_TIME_NO_SAVE" run_world_time_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_WORLD_TRANSITION_CONTEXT_GUARD" run_world_transition_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_DEBUG_INSPECT_OK" run_debug_inspect_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_WORLD_FLAGS_NO_SAVE" run_world_flags_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_ENTITIES_NO_DUNGEON" run_entities_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_ENTITY_SPAWN_NO_DUNGEON" run_entity_spawn_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_ENTITY_DESPAWN_OWNERSHIP_OK" run_entity_despawn_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_CHARACTER_REGISTERED:moonmod.headless-probe:headless-sentinel" run_character_registered_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_MODEL_RESOLVE_OK" run_model_resolve_at)
    string(FIND "${run_log}" "MOONMOD_API_STATUS|animations.register|supported" run_animations_register_at)
    string(FIND "${run_log}" "MOONMOD_API_STATUS|animations.play|supported" run_animations_play_at)
    string(FIND "${run_log}" "MOONMOD_API_STATUS|animations.stop|supported" run_animations_stop_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_ANIMATIONS_NO_DUNGEON_OK|moonmod.headless-probe:headless-idle" run_animations_no_dungeon_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_LOCATIONS_OK|3" run_locations_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_LOCATION_REGISTERED|chronicle-two:location:moonmod.headless-probe:moon-cave" run_location_registered_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_CHARACTER_SPAWN_NO_DUNGEON" run_character_spawn_no_dungeon_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_PLAYER_SET_NO_SAVE" run_player_set_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_FUTURE_DOMAINS_UNSUPPORTED" run_future_domains_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_GEORAMA_NO_SAVE" run_georama_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_SCRIPT_FUNCTIONS_OK" run_script_functions_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_SKILLS_OK" run_skills_at)
    string(FIND "${run_log}" "MOONMOD_HEADLESS_QUESTS_OK" run_quests_at)
    if(run_api_ok_at LESS 0 OR run_item_at LESS 0 OR run_dungeon_at LESS 0 OR run_scene_at LESS 0 OR
       run_physics_velocity_set_at LESS 0 OR run_physics_velocity_set_guard_at LESS 0 OR
       run_combat_at LESS 0 OR run_combat_damage_at LESS 0 OR run_world_time_at LESS 0 OR run_world_transition_at LESS 0 OR run_debug_inspect_at LESS 0 OR run_world_flags_at LESS 0 OR run_entities_at LESS 0 OR run_entity_spawn_at LESS 0 OR run_entity_despawn_at LESS 0 OR run_character_registered_at LESS 0 OR run_model_resolve_at LESS 0 OR run_locations_at LESS 0 OR run_character_spawn_no_dungeon_at LESS 0 OR run_player_set_at LESS 0 OR
       run_future_domains_at LESS 0 OR run_georama_at LESS 0 OR run_script_functions_at LESS 0 OR run_skills_at LESS 0 OR
       run_animations_register_at LESS 0 OR run_animations_play_at LESS 0 OR run_animations_stop_at LESS 0 OR run_animations_no_dungeon_at LESS 0 OR
       run_location_registered_at LESS 0 OR
       run_quests_at LESS 0)
        message(FATAL_ERROR "ChronicleTwo headless launch ${run_number} missed an API/content/scene assertion; inspect the log above.")
    endif()
endforeach()
if(supported_model_resolve_at LESS 0 OR model_resolve_at LESS 0 OR
   supported_physics_velocity_set_at LESS 0 OR physics_velocity_set_guard_at LESS 0 OR
   supported_locations_list_at LESS 0 OR supported_locations_get_at LESS 0 OR
   supported_locations_register_at LESS 0 OR location_registered_at LESS 0 OR locations_ok_at LESS 0 OR
   supported_player_set_at LESS 0 OR player_set_no_save_at LESS 0 OR future_domains_unsupported_at LESS 0 OR
   supported_animations_register_at LESS 0 OR supported_animations_play_at LESS 0 OR supported_animations_stop_at LESS 0 OR animations_no_dungeon_at LESS 0 OR
   skills_ok_at LESS 0 OR
   supported_script_register_at LESS 0 OR supported_script_list_at LESS 0 OR supported_script_call_at LESS 0 OR
   supported_script_unregister_at LESS 0 OR script_functions_ok_at LESS 0 OR
   supported_player_status_add_at LESS 0 OR supported_player_status_remove_at LESS 0 OR player_status_no_save_at LESS 0 OR
   supported_combat_at LESS 0 OR supported_combat_heal_at LESS 0 OR supported_combat_damage_at LESS 0 OR
   combat_damage_no_dungeon_at LESS 0 OR combat_no_dungeon_at LESS 0 OR
   supported_world_time_at LESS 0 OR world_time_no_save_at LESS 0 OR
   supported_world_transition_at LESS 0 OR world_transition_guard_at LESS 0 OR
   supported_debug_inspect_at LESS 0 OR debug_inspect_at LESS 0 OR
   supported_world_story_flag_get_at LESS 0 OR supported_world_story_flag_set_at LESS 0 OR
   supported_world_story_value_get_at LESS 0 OR supported_world_story_value_set_at LESS 0 OR
   supported_world_map_flag_get_at LESS 0 OR supported_world_map_flag_set_at LESS 0 OR world_flags_no_save_at LESS 0 OR
   supported_entities_query_at LESS 0 OR supported_entities_get_at LESS 0 OR supported_entities_spawn_at LESS 0 OR
   supported_entities_despawn_at LESS 0 OR entities_no_dungeon_at LESS 0 OR entity_spawn_no_dungeon_at LESS 0 OR entity_despawn_ownership_at LESS 0 OR
   supported_characters_spawn_at LESS 0 OR character_spawn_no_dungeon_at LESS 0 OR
   supported_party_set_active_at LESS 0 OR party_set_active_no_save_at LESS 0 OR
   supported_party_recruit_at LESS 0 OR party_recruit_no_save_at LESS 0 OR
   supported_quests_get_at LESS 0 OR supported_quests_register_at LESS 0 OR
   supported_quests_start_at LESS 0 OR supported_quests_advance_at LESS 0 OR
   quests_capabilities_at LESS 0 OR quests_lifecycle_at LESS 0 OR quests_persisted_at LESS 0 OR quests_ok_at LESS 0)
    message(FATAL_ERROR "ChronicleTwo headless player and party API verification failed; inspect the log above.")
endif()
string(REPLACE "\r\n" "\n" normalized_api_log "${log}")
string(REPLACE "\n" ";" api_log_lines "${normalized_api_log}")
set(api_status_lines "")
set(api_catalog_operation_count "")
foreach(api_log_line IN LISTS api_log_lines)
    string(FIND "${api_log_line}" "MOONMOD_API_STATUS|" api_status_marker_at)
    if(api_status_marker_at GREATER -1)
        list(APPEND api_status_lines "${api_log_line}")
    endif()
    string(FIND "${api_log_line}" "MOONMOD_API_CATALOG_COUNT|" catalog_count_marker_at)
    if(catalog_count_marker_at GREATER -1)
        string(LENGTH "MOONMOD_API_CATALOG_COUNT|" catalog_marker_length)
        math(EXPR catalog_payload_start "${catalog_count_marker_at} + ${catalog_marker_length}")
        string(SUBSTRING "${api_log_line}" ${catalog_payload_start} -1 api_catalog_operation_count)
        string(STRIP "${api_catalog_operation_count}" api_catalog_operation_count)
    endif()
endforeach()
list(LENGTH api_status_lines api_status_line_count)
set(api_report_valid TRUE)
set(api_report_operations 0)
set(api_report_unsupported 0)
set(api_report_paths "")
set(api_report_json "{\"format_version\":1,\"game_id\":\"chronicle-two\",\"operations\":[")
set(api_report_separator "")
if(api_status_line_count LESS 2 OR api_status_line_count GREATER 512)
    set(api_report_valid FALSE)
else()
    math(EXPR api_report_operations "${api_status_line_count} / 2")
    math(EXPR expected_status_lines "${api_report_operations} * 2")
    if(NOT api_status_line_count EQUAL expected_status_lines)
        set(api_report_valid FALSE)
    endif()
    math(EXPR last_api_index "${api_report_operations} - 1")
    foreach(api_index RANGE 0 ${last_api_index})
        math(EXPR repeat_index "${api_index} + ${api_report_operations}")
        list(GET api_status_lines ${api_index} status_line)
        list(GET api_status_lines ${repeat_index} repeated_status_line)
        if(NOT "${status_line}" STREQUAL "${repeated_status_line}")
            set(api_report_valid FALSE)
        endif()
        string(FIND "${status_line}" "MOONMOD_API_STATUS|" status_marker_at)
        string(LENGTH "MOONMOD_API_STATUS|" status_marker_length)
        math(EXPR status_payload_start "${status_marker_at} + ${status_marker_length}")
        string(SUBSTRING "${status_line}" ${status_payload_start} -1 status_payload)
        string(FIND "${status_payload}" "|" path_delimiter_at)
        if(path_delimiter_at LESS 1)
            set(api_report_valid FALSE)
        else()
            string(SUBSTRING "${status_payload}" 0 ${path_delimiter_at} api_path)
            math(EXPR status_part_start "${path_delimiter_at} + 1")
            string(SUBSTRING "${status_payload}" ${status_part_start} -1 status_and_reason)
            string(FIND "${status_and_reason}" "|" reason_delimiter_at)
            if(reason_delimiter_at LESS 0)
                set(api_report_valid FALSE)
                set(api_status "invalid")
                set(api_reason "missing reason field")
            else()
                string(SUBSTRING "${status_and_reason}" 0 ${reason_delimiter_at} api_status)
                math(EXPR reason_part_start "${reason_delimiter_at} + 1")
                string(SUBSTRING "${status_and_reason}" ${reason_part_start} -1 api_reason)
                string(STRIP "${api_reason}" api_reason)
            endif()
            list(FIND api_report_paths "${api_path}" duplicate_path)
            if(duplicate_path GREATER -1 OR (NOT api_status STREQUAL "supported" AND NOT api_status STREQUAL "unsupported"))
                set(api_report_valid FALSE)
            endif()
            list(APPEND api_report_paths "${api_path}")
            if(api_status STREQUAL "unsupported")
                math(EXPR api_report_unsupported "${api_report_unsupported} + 1")
            endif()
            string(FIND "${api_path}" "." domain_end)
            if(domain_end GREATER 0)
                string(SUBSTRING "${api_path}" 0 ${domain_end} api_domain)
            else()
                set(api_domain "${api_path}")
            endif()
            string(REPLACE "\\" "\\\\" safe_reason "${api_reason}")
            string(REPLACE "\"" "\\\"" safe_reason "${safe_reason}")
            string(APPEND api_report_json "${api_report_separator}{\"path\":\"${api_path}\",\"domain\":\"${api_domain}\",\"status\":\"${api_status}\",\"reason\":\"${safe_reason}\"}")
            set(api_report_separator ",")
        endif()
    endforeach()
endif()
if(api_catalog_operation_count STREQUAL "")
    set(api_report_valid FALSE)
elseif(NOT api_report_operations EQUAL api_catalog_operation_count)
    set(api_report_valid FALSE)
endif()
string(APPEND api_report_json "],\"operation_count\":${api_report_operations},\"unsupported_count\":${api_report_unsupported}}")
file(WRITE "$ENV{DC2_SAVE}/moonmod-api-report.json" "${api_report_json}")
if(NOT EXISTS "$ENV{DC2_SAVE}/moonmod-api-report.json")
    set(api_report_valid FALSE)
else()
    file(READ "$ENV{DC2_SAVE}/moonmod-api-report.json" api_report_contents)
    string(JSON verified_operation_count ERROR_VARIABLE api_report_parse_error LENGTH "${api_report_contents}" operations)
    if(NOT api_report_parse_error STREQUAL "NOTFOUND" OR NOT verified_operation_count EQUAL api_report_operations)
        set(api_report_valid FALSE)
    endif()
endif()
if(rc EQUAL 0 AND second_rc EQUAL 0 AND index_at GREATER -1 AND frame_at GREATER -1 AND
   api_error_at EQUAL -1 AND callback_error_at EQUAL -1 AND moonmod_at GREATER -1 AND
   supported_item_at GREATER -1 AND supported_items_list_at GREATER -1 AND supported_items_get_at GREATER -1 AND
   supported_items_use_at GREATER -1 AND pending_items_listed_at GREATER -1 AND
   supported_localization_register_at GREATER -1 AND supported_localization_get_at GREATER -1 AND localization_at GREATER -1 AND
   supported_input_bind_at GREATER -1 AND supported_input_state_at GREATER -1 AND supported_input_consume_at GREATER -1 AND
   supported_input_unbind_at GREATER -1 AND supported_input_context_push_at GREATER -1 AND
   supported_input_context_pop_at GREATER -1 AND supported_input_context_get_at GREATER -1 AND
   input_ok_at GREATER -1 AND input_contexts_at GREATER -1 AND
   supported_physics_velocity_at GREATER -1 AND physics_velocity_guard_at GREATER -1 AND
   supported_physics_velocity_set_at GREATER -1 AND physics_velocity_set_guard_at GREATER -1 AND
   supported_resources_load_at GREATER -1 AND supported_resources_resolve_at GREATER -1 AND
   supported_resources_release_at GREATER -1 AND resources_ok_at GREATER -1 AND
   supported_ui_notify_at GREATER -1 AND ui_notify_ok_at GREATER -1 AND
   supported_ui_register_at GREATER -1 AND supported_ui_remove_at GREATER -1 AND ui_actions_ok_at GREATER -1 AND
   supported_debug_profile_at GREATER -1 AND debug_profile_ok_at GREATER -1 AND
   supported_weapons_list_at GREATER -1 AND supported_weapons_get_at GREATER -1 AND supported_weapons_equip_at GREATER -1 AND
   supported_weapons_upgrade_at GREATER -1 AND supported_weapons_register_at GREATER -1 AND weapons_empty_at GREATER -1 AND
   weapons_equip_empty_at GREATER -1 AND weapons_upgrade_empty_at GREATER -1 AND
   weapon_pending_at GREATER -1 AND content_registry_at GREATER -1 AND content_unregister_at GREATER -1 AND content_all_types_at GREATER -1 AND item_pending_at GREATER -1 AND supported_dungeon_at GREATER -1 AND
   supported_dungeons_list_at GREATER -1 AND supported_dungeons_get_at GREATER -1 AND supported_dungeons_register_at GREATER -1 AND
   supported_model_resolve_at GREATER -1 AND model_resolve_at GREATER -1 AND
   supported_model_register_at GREATER -1 AND supported_model_unregister_at GREATER -1 AND
   model_unregister_at GREATER -1 AND model_register_at GREATER -1 AND
   supported_player_at GREATER -1 AND supported_player_set_at GREATER -1 AND player_set_no_save_at GREATER -1 AND
   future_domains_unsupported_at GREATER -1 AND
   supported_player_damage_at GREATER -1 AND supported_party_at GREATER -1 AND
   supported_characters_list_at GREATER -1 AND supported_characters_get_at GREATER -1 AND
   supported_characters_register_at GREATER -1 AND characters_registered_at GREATER -1 AND
   supported_economy_get_at GREATER -1 AND supported_economy_add_at GREATER -1 AND supported_economy_spend_at GREATER -1 AND
   supported_item_remove_at GREATER -1 AND supported_save_migrate_at GREATER -1 AND supported_world_at GREATER -1 AND
   world_query_at GREATER -1 AND dungeon_pending_at GREATER -1 AND storage_initial_at GREATER -1 AND
   storage_reload_at GREATER -1 AND save_migrated_at GREATER -1 AND save_migration_reloaded_at GREATER -1 AND
   api_report_valid AND EXISTS "$ENV{DC2_SAVE}/moonmod-api-report.txt" AND
   EXISTS "$ENV{DC2_SAVE}/moonmod-api-report.json" AND sig STREQUAL "89504e470d0a1a0a")
    message("MOONMOD_SMOKE_OK")
else()
    message(FATAL_ERROR "ChronicleTwo headless MoonMod verification failed; inspect the report and log above.")
endif()
]===])
add_test(NAME MoonMod.HeadlessProbe
    COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:darkchronicle>
        -DROOT=${CMAKE_BINARY_DIR}/moonmod-smoke-fixture
        -DEMPTY_DATA_FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/empty_data.cmake
        -DDATA_VERSION_SOURCE=${KZ_ROOT}/data_version.json -P ${MOONMOD_SMOKE_SCRIPT})
set_tests_properties(MoonMod.HeadlessProbe PROPERTIES
    PASS_REGULAR_EXPRESSION "MOONMOD_SMOKE_OK"
    ENVIRONMENT "DC2_SAVE=${CMAKE_BINARY_DIR}/moonmod-smoke-fixture/save;DC2_DATA=${CMAKE_BINARY_DIR}/moonmod-smoke-fixture/data"
    TIMEOUT 300 RUN_SERIAL TRUE LABELS gpu)

file(GLOB ct_stream_cmake CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.cmake)
foreach(f IN LISTS ct_stream_cmake)
    include(${f})
endforeach()

add_test(NAME Gpu.PipelineWarmup
    COMMAND darkchronicle_tests
        --gtest_fail_if_no_test_selected
        "--gtest_filter=GfxGpuMeshDc2.PipelinesOnlyWhenAsked:DynamicMask/GfxGpuColorWriteMask.DepthOnlyDrawsKeepTheirDepthAndLeaveColourAlone/0")
set_tests_properties(Gpu.PipelineWarmup PROPERTIES
    FIXTURES_SETUP gpu_pipeline_cache TIMEOUT 600 RUN_SERIAL TRUE LABELS gpu RESOURCE_LOCK renderer
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
get_property(ct_registered_tests DIRECTORY PROPERTY TESTS)
foreach(test IN LISTS ct_registered_tests)
    get_test_property(${test} LABELS labels)
    if("gpu" IN_LIST labels AND NOT test STREQUAL "Gpu.PipelineWarmup")
        set_property(TEST ${test} APPEND PROPERTY FIXTURES_REQUIRED gpu_pipeline_cache)
    endif()
endforeach()
