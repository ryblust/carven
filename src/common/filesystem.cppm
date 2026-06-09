module;

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

export module carven.common.filesystem;

import std;

export auto map_file(std::string_view path) noexcept -> std::optional<std::pair<void*, std::size_t>> {
#if defined(_WIN32)
    const auto wide_len = MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), nullptr, 0);
    if (wide_len == 0) return std::nullopt;

    auto wide_path = std::wstring(wide_len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), wide_path.data(), wide_len);

    const auto handle = CreateFileW(wide_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return std::nullopt;

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(handle, &file_size)) {
        CloseHandle(handle);
        return std::nullopt;
    }

    const auto size = static_cast<std::size_t>(file_size.QuadPart);
    void* data = nullptr;

    if (size > 0) {
        const auto mapping = CreateFileMappingW(handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
        CloseHandle(handle);
        if (!mapping) return std::nullopt;

        data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        CloseHandle(mapping);
        if (!data) return std::nullopt;
    } else {
        CloseHandle(handle);
    }

    return std::pair { data, size };
#else
    const auto fd = ::open(path.data(), O_RDONLY);
    if (fd < 0) return std::nullopt;

    struct stat st;
    if (::fstat(fd, &st) < 0) {
        ::close(fd);
        return std::nullopt;
    }

    const auto size = static_cast<std::size_t>(st.st_size);
    void* data = nullptr;

    if (size > 0) {
        data = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (data == MAP_FAILED) {
            ::close(fd);
            return std::nullopt;
        }
    }

    ::close(fd);
    return std::pair { data, size };
#endif
}

export auto unmap_file(void* data, std::size_t size) noexcept -> void {
#if defined(_WIN32)
    (void)size;
    if (data) UnmapViewOfFile(data);
#else
    if (data) ::munmap(data, size);
#endif
}
