#include <memory>
#include <string>

#include "corona/pal/i_dynamic_library.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Corona::PAL {

class WinDynamicLibrary : public IDynamicLibrary {
   public:
    WinDynamicLibrary() : handle_(nullptr) {}

    ~WinDynamicLibrary() override {
        unload();
    }

    bool load(std::string_view path) override {
        int path_length = MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), nullptr, 0);
        wchar_t* wide_path = new wchar_t[path_length + 1];
        MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), wide_path, path_length);
        wide_path[path_length] = L'\0';

        handle_ = LoadLibraryW(wide_path);
        delete[] wide_path;

        return handle_ != nullptr;
    }

    void unload() override {
        if (handle_) {
            FreeLibrary(handle_);
            handle_ = nullptr;
        }
    }

    FunctionPtr get_symbol(std::string_view name) override {
        if (!handle_) {
            return nullptr;
        }

        // name 必须是 null-terminated 的，所以我们创建一个临时字符串
        std::string name_str(name);
        return reinterpret_cast<FunctionPtr>(GetProcAddress(handle_, name_str.c_str()));
    }

   private:
    HMODULE handle_;
};

// Factory function implementation
std::unique_ptr<IDynamicLibrary> create_dynamic_library() {
    return std::make_unique<WinDynamicLibrary>();
}

}  // namespace Corona::PAL

#endif  // _WIN32
