#include "common.h"

#include "debug_mode.hpp"
#include "mainloop.hpp"
#include "title.hpp"

void PortApplyDebugMode(bool enabled) {
    DebugFlag = enabled;
    MasterDebugCode = enabled ? MASTER_DEBUG_CODE : 0;
    MasterDebugModeOn = enabled;
}

void PortToggleDebugMode() {
    DebugFlag = !DebugFlag;
}
