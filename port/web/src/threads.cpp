// Thread functions quill's backend calls that Emscripten's libc declares but leaves out (emscripten #12216:
// pthread_setname_np.c and pthread_getname_np.c are skipped from musl, and sched has only sched_yield). The
// names are kept per thread, for the thread itself: the log's backend names itself and each thread asks its own
// name. There is no CPU affinity in a browser; quill asks for it only when the game configures one, which it never
// does.

#include <pthread.h>
#include <sched.h>

#include <cerrno>
#include <cstring>

namespace {

thread_local char g_thread_name[16] = {};

} // namespace

extern "C" {

int pthread_setname_np(pthread_t thread, const char *name) {
    if (thread != pthread_self()) {
        return ENOSYS;
    }
    std::strncpy(g_thread_name, name, sizeof(g_thread_name) - 1);
    g_thread_name[sizeof(g_thread_name) - 1] = '\0';
    return 0;
}

int pthread_getname_np(pthread_t thread, char *name, size_t len) {
    if (len == 0) {
        return ERANGE;
    }
    if (thread != pthread_self()) {
        name[0] = '\0';
        return 0;
    }
    if (std::strlen(g_thread_name) >= len) {
        return ERANGE;
    }
    std::strcpy(name, g_thread_name);
    return 0;
}

int sched_setaffinity(pid_t, size_t, const cpu_set_t *) {
    errno = ENOSYS;
    return -1;
}

} // extern "C"
