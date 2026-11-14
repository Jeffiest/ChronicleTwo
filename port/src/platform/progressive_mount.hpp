#pragma once

#include <memory>

#include "platform/vfs.hpp"

/**
 * Mounts the data.zip a progressive extraction is still writing (Kazarov's progressive session, which the web's
 * first run starts when the player chose to unpack the disc while playing): every name is listed from the
 * start, and a read of an entry not converted yet waits for its conversion, which the session moves to the
 * front. Null when no such extraction is ready; throws std::runtime_error when its names differ only by ASCII
 * case.
 */
std::shared_ptr<const vfs::Mount> ProgressiveMountOpen();
