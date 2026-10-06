#include "playlist_image.hpp"
#include <cstdint>

namespace rankedpractice {
std::string EncodePlaylistImage(std::string_view bytes, std::string_view mediaType) {
    if (bytes.empty()) return {};
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output = "data:" + std::string(mediaType) + ";base64,";
    output.reserve(output.size() + (bytes.size() + 2) / 3 * 4);
    for (size_t index = 0; index < bytes.size(); index += 3) {
        const auto remaining = bytes.size() - index;
        const unsigned int block = (static_cast<unsigned char>(bytes[index]) << 16) |
            (remaining > 1 ? static_cast<unsigned char>(bytes[index + 1]) << 8 : 0U) |
            (remaining > 2 ? static_cast<unsigned char>(bytes[index + 2]) : 0U);
        output.push_back(alphabet[(block >> 18) & 0x3F]);
        output.push_back(alphabet[(block >> 12) & 0x3F]);
        output.push_back(remaining > 1 ? alphabet[(block >> 6) & 0x3F] : '=');
        output.push_back(remaining > 2 ? alphabet[block & 0x3F] : '=');
    }
    return output;
}

std::string DownloadedPlaylistImage(std::string_view bytes) {
    if (bytes.size() >= 45 && bytes.substr(0, 8) == std::string_view("\x89PNG\r\n\x1a\n", 8) &&
        bytes.substr(12, 4) == "IHDR" && bytes.substr(bytes.size() - 8, 4) == "IEND") {
        auto dimension = [&](size_t offset) {
            uint32_t value = 0;
            for (size_t i = 0; i < 4; ++i) value = (value << 8) | static_cast<unsigned char>(bytes[offset + i]);
            return value;
        };
        const auto width = dimension(16), height = dimension(20);
        if (width > 0 && height > 0 && width <= 4096 && height <= 4096)
            return EncodePlaylistImage(bytes, "image/png");
    }
    if (bytes.size() >= 20 && bytes.substr(0, 3) == std::string_view("\xff\xd8\xff", 3) &&
        bytes.substr(bytes.size() - 2) == std::string_view("\xff\xd9", 2))
        return EncodePlaylistImage(bytes, "image/jpeg");
    return {};
}
} // namespace rankedpractice
