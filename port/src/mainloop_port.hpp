#pragma once

// The seam between the driver (port/src/main.cpp) and the game's main loop: what the driver sets
// before MainLoop and what the loop reads at the frame boundary. The loop's own state stays in
// mainloop.cpp; these are the only handles the driver has on it.

// The game headers keep MWCC's pragmas, which clang names but ignores.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#include "mainloop.hpp"
#pragma clang diagnostic pop

/**
 * Asks the loop to stop. Read at the top of a frame, so the frame that was running when the
 * request came in finishes and the loop leaves between frames the way it leaves when a mode
 * ends: the mode gets its LoopExit and the boot's shutdown still runs. Closing the window is
 * the caller.
 */
void GameRequestStop();

/**
 * Whether GameRequestStop has been called. The loop clears nothing.
 */
bool GameStopRequested();

/**
 * Frames MainLoop has run, one per mgEndFrame it calls itself (the presents a mode makes on its
 * own are not counted). Zero through the warm-up; the first frame's mgEndFrame makes it 1.
 */
s64 GameFrameCount();

/**
 * Stops the loop once it has run this many frames; negative is no budget. Checked with
 * GameRequestStop at the frame boundary, so exactly this many frames run. --frames is the caller.
 */
void GameSetFrameBudget(s64 frames);

/**
 * Boots the loop in a mode instead of the title: LoopNo becomes loop_no (a MainLoopMode), the
 * mode's LoopInit is handed arg, and DebugFlag starts as debug_flag. skip_warmup also skips the
 * boot's language and logo screens, the pad waits and the debug chord, so the first frame runs
 * at once. Set before MainLoop; a later call replaces the mode. --jump is the caller.
 */
void GameSetStartMode(int loop_no, INIT_LOOP_ARG arg, int debug_flag, bool skip_warmup);
