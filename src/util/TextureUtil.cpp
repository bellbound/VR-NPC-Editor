#include "util/TextureUtil.h"

#include <spdlog/spdlog.h>

namespace TextureUtil {
    namespace {
        bool IsPowerOfTwo(uint32_t value) {
            return value != 0 && (value & (value - 1)) == 0;
        }

        bool StartsWithNoCase(const std::string& text, std::string_view prefix) {
            if (text.size() < prefix.size()) return false;
            for (size_t i = 0; i < prefix.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(text[i])) != prefix[i]) return false;
            }
            return true;
        }
    }

    std::string Normalize(std::string path) {
        for (auto& c : path) {
            if (c == '/') c = '\\';
        }

        while (!path.empty() && path.front() == '\\') path.erase(path.begin());

        if (StartsWithNoCase(path, "data\\")) path.erase(0, 5);

        return path;
    }

    bool Exists(const std::string& dataRelativePath) {
        if (dataRelativePath.empty()) return false;

        RE::BSResourceNiBinaryStream stream(dataRelativePath);
        return stream.good();
    }

    bool IsUsableSwatch(const std::string& dataRelativePath, uint32_t& width, uint32_t& height) {
        width = 0;
        height = 0;
        if (dataRelativePath.empty()) return false;

        RE::BSResourceNiBinaryStream stream(dataRelativePath);
        if (!stream.good()) return false;

        // DDS header: "DDS " magic, dwSize, dwFlags, dwHeight, dwWidth
        struct {
            char          magic[4];
            std::uint32_t size;
            std::uint32_t flags;
            std::uint32_t height;
            std::uint32_t width;
        } header{};

        if (!stream.read(reinterpret_cast<char*>(&header), sizeof(header))) return false;
        if (std::memcmp(header.magic, "DDS ", 4) != 0) return false;

        width = header.width;
        height = header.height;
        return IsPowerOfTwo(width) && IsPowerOfTwo(height);
    }
}
