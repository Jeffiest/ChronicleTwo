#pragma once

// Applies the host config before the title loop starts. The retail title only unlocks its debug
// route when both values are set, and the developer build also needs DebugFlag enabled.
void PortApplyDebugMode(bool enabled);

// Toggle the retail debug flag from the gamepad or a host input binding.
void PortToggleDebugMode();
