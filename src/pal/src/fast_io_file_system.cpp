#include <fast_io.h>
#include <fast_io_device.h>

#include <memory>
#include <vector>

#include "corona/pal/i_file_system.h"

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
};

// Factory function implementation
std::unique_ptr<IFileSystem> create_file_system() {
    return std::make_unique<FastIoFileSystem>();
}

}  // namespace Corona::PAL
