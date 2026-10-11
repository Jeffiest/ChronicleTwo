#include "platform/settings.hpp"

namespace {

HostSettings &Current() {
    static HostSettings settings;
    return settings;
}

} // namespace

const HostSettings &SettingsGet() {
    return Current();
}

bool SettingsChange(const HostSettings &settings) {
    Current() = settings;
    return true;
}
