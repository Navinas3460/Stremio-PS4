// Local helpers for the phone-based add-on configuration flow.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace addon_config {

// Only supports the standard HTTP(S) /manifest.json -> /configure route.
// Returns empty for unsupported or ambiguous addresses.
std::string configure_url(const std::string &manifest_url);

// Generates a QR locally. On failure returns an empty vector.
// QR data must not be sent to external image services or written to logs.
std::vector<uint8_t> make_qr(const std::string &text);
int qr_size(const std::vector<uint8_t> &code);
bool qr_dark(const std::vector<uint8_t> &code, int x, int y);

} // namespace addon_config
