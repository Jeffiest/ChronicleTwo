#include <cstdio>
#include <cstdlib>

#include "exitcodes.hpp"

/**
 *
 * Reports a PS2-only path reached on the host and stops, so an unfinished
 * replacement is loud instead of silent.
 *
 */
[[noreturn]] void Ps2Unimplemented(const char *function, const char *file, int line) {
    std::fprintf(stderr, "%s:%d: %s: PlayStation 2 code is not implemented on PC\n", file, line, function);
    std::abort();
}

/**
 *
 * The Metrowerks runtime's assertion failure. Retail halted after printing
 * the expression, so the port stops the same way.
 *
 */
[[noreturn]] void __assert(const char *file, int line, const char *expression) {
    std::fflush(stdout);
    std::fprintf(stderr, "%s:%d: assertion failed: %s\n", file, line, expression);
    std::fflush(nullptr);
    std::_Exit(kExitGameAssert);
}

/**
 *
 * The Metrowerks runtime's float-to-signed-integer helper. MWCC called it for
 * conversions the MIPS fptosi.s instruction could not express directly.
 *
 */
extern "C" int fptosi(float value) {
    return static_cast<int>(value);
}
