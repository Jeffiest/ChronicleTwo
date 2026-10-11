#pragma once

#ifdef _WIN32
#include <cstring>

// The CRT has no BSD index().
inline char *index(const char *string, int c) {
    return const_cast<char *>(std::strchr(string, c));
}
#endif
