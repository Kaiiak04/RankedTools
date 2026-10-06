#pragma once

#include <string>
#include <string_view>

namespace rankedpractice {
std::string EncodePlaylistImage(std::string_view bytes, std::string_view mediaType);
// Recognize the PNG/JPEG formats supported by Unity's playlist image loader.
// Empty, truncated, HTML, and unsupported payloads return no image.
std::string DownloadedPlaylistImage(std::string_view bytes);
} // namespace rankedpractice
