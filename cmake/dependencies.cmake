include(FetchContent)

FetchContent_Declare(
    fast_io
    GIT_REPOSITORY https://github.com/cppfastio/fast_io.git
    GIT_TAG master  # You might want to pin this to a specific commit or tag
)
FetchContent_MakeAvailable(fast_io)
