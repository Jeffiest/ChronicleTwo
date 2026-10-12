#pragma once

#include <filesystem>

std::filesystem::path MoviePortPath(const char *name);

// Driver port 5: GetCSndPortNo never maps a game port to it, so no sequence or sound effect plays there during a movie.
inline constexpr int kMoviePortAudioPort = 5;
