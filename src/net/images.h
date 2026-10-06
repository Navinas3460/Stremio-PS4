// Remote artwork: downloaded and decoded (stb_image) on HTTP worker threads,
// uploaded as textures on the main thread, kept in an LRU cache.
#pragma once

#include "../render/renderer.h"

#include <string>
#include <vector>

namespace images {

enum class State { LOADING, READY, FAILED };

// Returns the texture for `url` if it's ready; otherwise starts loading it
// (once) and returns an invalid texture. Call every frame for visible items.
const Texture &get(const std::string &url, State *state = nullptr);

// First ready texture among `urls` (artwork fallbacks, best first); keeps
// loading the next candidate while earlier ones fail.
const Texture &first(const std::vector<std::string> &urls);

// Uploads decoded images (a few per frame) and evicts old ones. Main thread.
void update();

} // namespace images
