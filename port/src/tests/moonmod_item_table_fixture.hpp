#pragma once

#include <cstring>

#include "gamedata.hpp"
#include "scriptinterpreter.hpp"
#include "stubs/gamedata.hpp"

// The item table is a JSON document now, without the DATACOMINIT/DATACOM script tags the MoonMod tests seeded it
// with; these do what those tags did, on the same game state.
namespace moonmod_test {

/** Starts a common item table of stack->value.integer records, as DATACOMINIT did. */
inline int ItemTableInit(SPI_STACK *stack, int) {
    GameItemDataManage.common_num = stack->value.integer;
    comdatapt_num = 0;
    comdatapt = GameItemDataManage.common_data;
    std::memset(local_itemdatano_converttable, -1, sizeof(local_itemdatano_converttable));
    return 1;
}

/**
 * Adds one common item record, as DATACOM did: item number, type, list number, active set, stack, maximum,
 * icon texture, icon, message, file name and attribute.
 */
inline int ItemTableAdd(SPI_STACK *stack, int) {
    CDataCommon *record = comdatapt;
    record->item_no = static_cast<short>((stack++)->value.integer);
    record->type = static_cast<short>((stack++)->value.integer);
    record->list_no = static_cast<short>((stack++)->value.integer);
    record->active_set = static_cast<short>((stack++)->value.integer);
    record->stack_num = static_cast<short>((stack++)->value.integer);
    record->max_num = static_cast<short>((stack++)->value.integer);
    record->icon_texture_no = static_cast<short>((stack++)->value.integer);
    record->icon_no = static_cast<short>((stack++)->value.integer);
    record->message_no = static_cast<short>((stack++)->value.integer);
    if (const char *name = (stack++)->value.string; name != nullptr) {
        std::strncpy(record->file_name, name, sizeof(record->file_name) - 1);
    }
    record->attribute = static_cast<decltype(record->attribute)>(stack->value.integer);
    record->name = nullptr;
    local_itemdatano_converttable[record->item_no] = static_cast<short>(comdatapt_num);
    comdatapt_num += 1;
    comdatapt++;
    return 1;
}

} // namespace moonmod_test
