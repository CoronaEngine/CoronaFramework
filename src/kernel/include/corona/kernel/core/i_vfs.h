#pragma once
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Corona::Kernel {

class IVirtualFileSystem {
   public:
    virtual ~IVirtualFileSystem() = default;

    // Mount a physical path to a virtual path
    virtual bool mount(std::string_view virtual_path, std::string_view physical_path) = 0;

    // Unmount a virtual path
    virtual void unmount(std::string_view virtual_path) = 0;

    // Resolve a virtual path to a physical path
    virtual std::string resolve(std::string_view virtual_path) const = 0;

    // File operations using virtual paths
    virtual std::vector<std::byte> read_file(std::string_view virtual_path) = 0;
    virtual bool write_file(std::string_view virtual_path, std::span<const std::byte> data) = 0;
    virtual bool exists(std::string_view virtual_path) = 0;

    // Directory operations
    virtual std::vector<std::string> list_directory(std::string_view virtual_path) = 0;
    virtual bool create_directory(std::string_view virtual_path) = 0;
};

}  // namespace Corona::Kernel
