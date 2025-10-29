#pragma once
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Corona::PAL {
class IFileSystem {
   public:
    virtual ~IFileSystem() = default;
    virtual std::vector<std::byte> read_all_bytes(std::string_view path) = 0;
    virtual bool write_all_bytes(std::string_view path, std::span<const std::byte> data) = 0;
    virtual bool exists(std::string_view path) = 0;

    // Directory operations
    virtual bool create_directory(std::string_view path) = 0;
    virtual std::vector<std::string> list_directory(std::string_view path) = 0;
};
}  // namespace Corona::PAL
