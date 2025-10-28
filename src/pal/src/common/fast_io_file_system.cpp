#include <fast_io.h>
#include <fast_io_device.h>

#include <filesystem>
#include <memory>
#include <vector>

#include "corona/pal/i_file_system.h"

namespace fs = std::filesystem;

namespace Corona::PAL {

class FastIoFileSystem : public IFileSystem {
   public:
    std::vector<std::byte> read_all_bytes(std::string_view path) override {
        try {
            fast_io::native_file_loader loader(path);
            std::vector<std::byte> buffer(loader.size());
            std::memcpy(buffer.data(), loader.data(), loader.size());
            return buffer;
        } catch (const fast_io::error& e) {
            fast_io::io::perr("Failed to read file: ", path, "\n");
            return {};
        }
    }

    bool write_all_bytes(std::string_view path, std::span<const std::byte> data) override {
        try {
            fast_io::native_file file(path, fast_io::open_mode::out | fast_io::open_mode::trunc);
            write(file, reinterpret_cast<const char*>(data.data()),
                  reinterpret_cast<const char*>(data.data() + data.size()));
            return true;
        } catch (const fast_io::error& e) {
            fast_io::io::perr("Failed to write file: ", path, "\n");
            return false;
        }
    }

    bool exists(std::string_view path) override {
        try {
            // Try to open file for reading, if successful then it exists
            fast_io::native_file file(path, fast_io::open_mode::in);
            return true;
        } catch (...) {
            return false;
        }
    }

    bool create_directory(std::string_view path) override {
        try {
            std::error_code ec;
            fs::create_directories(fs::path(path), ec);
            return !ec;
        } catch (...) {
            return false;
        }
    }

    std::vector<std::string> list_directory(std::string_view path) override {
        std::vector<std::string> entries;
        try {
            for (const auto& entry : fs::directory_iterator(fs::path(path))) {
                entries.push_back(entry.path().filename().string());
            }
        } catch (...) {
            // Return empty vector on error
        }
        return entries;
    }
};

// Factory function implementation
std::unique_ptr<IFileSystem> create_file_system() {
    return std::make_unique<FastIoFileSystem>();
}

}  // namespace Corona::PAL
