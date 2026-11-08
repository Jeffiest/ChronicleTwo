// Crash reports on the web (crash/crash.hpp): no cpptrace, no signal handlers and no dialog. A failure is
// written to stderr, which the page's log drawer shows, and the process ends through WebQuickExit (exit.cpp),
// so the page hears the status.

#include "crash/crash.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>

#include "log/log.hpp"

[[noreturn]] void WebQuickExit(int status); // exit.cpp

namespace {

const char *KindName(CrashKind kind) {
    switch (kind) {
        case CrashKind::Assert:
            return "assertion";
        case CrashKind::Unimplemented:
            return "unimplemented";
        case CrashKind::Fatal:
            return "fatal error";
        case CrashKind::Abort:
            return "abort";
        case CrashKind::Terminate:
            return "terminate";
        case CrashKind::Signal:
            return "signal";
    }
    return "failure";
}

} // namespace

void CrashInit([[maybe_unused]] const CrashInitOptions &options) {
    std::set_terminate([] {
        if (const auto exception = std::current_exception()) {
            try {
                std::rethrow_exception(exception);
            } catch (const std::exception &error) {
                CrashReportAndExit(CrashKind::Terminate, error.what(), EXIT_FAILURE);
            } catch (...) {
            }
        }
        CrashReportAndExit(CrashKind::Terminate, "unhandled C++ exception", EXIT_FAILURE);
    });
}

void CrashSetBundleRoot([[maybe_unused]] const std::filesystem::path &save_root) {}

void CrashShowLastReportNotice() {}

[[noreturn]] void CrashReportAndExit(CrashKind kind, std::string_view message, int exit_status) {
    std::fflush(stdout);
    std::fprintf(stderr, "Dark Chronicle %s: %.*s\n", KindName(kind), static_cast<int>(message.size()), message.data());
    std::fflush(stderr);
    if (!LogIsBackendThread()) {
        LogFlush();
    }
    WebQuickExit(exit_status);
}
