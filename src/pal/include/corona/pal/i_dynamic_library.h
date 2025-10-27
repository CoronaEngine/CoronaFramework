#pragma once
#include <string_view>

namespace Corona::PAL {
using FunctionPtr = void (*)();

class IDynamicLibrary {
   public:
    virtual ~IDynamicLibrary() = default;
    virtual bool load(std::string_view path) = 0;
    virtual void unload() = 0;
    virtual FunctionPtr get_symbol(std::string_view name) = 0;
};
}  // namespace Corona::PAL
