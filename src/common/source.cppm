export module carven.common.source;

import carven.common.filemap;
import std;

export template<typename ... Ts>
struct Overloaded final : Ts... { using Ts::operator()...; };

export template<typename ... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

export struct Span final {
    std::uint32_t start;
    std::uint32_t end;

    constexpr auto empty() const noexcept -> bool {
        return start >= end;
    }
};

export struct SourceLocation final {
    std::uint32_t line;
    std::uint32_t column;
};

template<> struct std::formatter<SourceLocation> final {
    constexpr auto parse(const auto& context) const noexcept {
        return context.begin();
    }

    auto format(SourceLocation location, auto&& context) const noexcept {
        return std::format_to(context.out(), "{}:{}", location.line, location.column);
    }
};

export class LineOffsets final {
public:
    explicit constexpr LineOffsets(std::string_view content) noexcept {
        offsets.reserve(content.size() / 40 + 1);
        offsets.push_back(0);

        std::size_t pos = 0;
        while ((pos = content.find('\n', pos)) != std::string_view::npos) {
            offsets.push_back(static_cast<std::uint32_t>(pos + 1));
            ++pos;
        }
    }

    constexpr auto location(std::uint32_t offset) const noexcept -> SourceLocation {
        if (offsets.empty()) return { .line = 1, .column = 1 };

        const auto iter = std::ranges::upper_bound(offsets, offset);
        const auto index = static_cast<std::uint32_t>(iter - offsets.begin() - 1);

        return { .line = index + 1, .column = offset - offsets[index] + 1 };
    }

private:
    std::vector<std::uint32_t> offsets;
};

export constexpr auto slice(std::string_view text, Span span) noexcept -> std::string_view {
    if (span.start > span.end || span.end > text.size()) return {};
    return text.substr(span.start, span.end - span.start);
}

export class SourceFile final {
public:
    constexpr SourceFile(std::string_view content, std::string_view path = "<string>") noexcept
        : fpath(path), is_mmaped(false), source(content) {}

    [[nodiscard]] static auto from_file(std::string_view fpath) noexcept -> std::optional<SourceFile> {
        const auto mapped = map_file(fpath);
        if (!mapped) return std::nullopt;

        return SourceFile(fpath, mapped->first, mapped->second);
    }

    SourceFile(const SourceFile&) = delete;
    auto operator=(const SourceFile&) = delete;

    constexpr SourceFile(SourceFile&& other) noexcept : fpath(other.fpath), is_mmaped(other.is_mmaped) {
        if (is_mmaped) {
            mmap_data = other.mmap_data;
            mmap_size = other.mmap_size;
            other.mmap_data = nullptr;
            other.mmap_size = 0;
            other.source = {};
        } else {
            source = other.source;
            other.source = {};
        }
        other.is_mmaped = false;
    }

    constexpr auto operator=(SourceFile&& other) noexcept -> SourceFile& {
        if (this == &other) return *this;

        if !consteval {
            if (is_mmaped) {
                unmap_file(mmap_data, mmap_size);
            }
        }

        fpath = other.fpath;

        if !consteval {
            if (other.is_mmaped) {
                mmap_data = other.mmap_data;
                mmap_size = other.mmap_size;
                is_mmaped = true;

                other.mmap_data = nullptr;
                other.mmap_size = 0;
                other.is_mmaped = false;
                return *this;
            }
        }

        source = other.source;
        is_mmaped = false;
        other.source = {};
        other.is_mmaped = false;

        return *this;
    }

    constexpr ~SourceFile() noexcept {
        if !consteval {
            if (is_mmaped) {
                unmap_file(mmap_data, mmap_size);
            }
        }
    }

    constexpr auto filepath() const noexcept -> std::string_view { return fpath; }

    constexpr auto text() const noexcept -> std::string_view {
        if consteval { return source; }
        return is_mmaped
            ? std::string_view(mmap_data ? static_cast<const char*>(mmap_data) : "", mmap_size)
            : source;
    }

private:
    std::string_view fpath;
    bool is_mmaped;
    union {
        std::string_view source;
        struct {
            void* mmap_data;
            std::size_t mmap_size;
        };
    };

    constexpr SourceFile(std::string_view path, void* data, std::size_t size) noexcept
        : fpath(path)
        , is_mmaped(true)
        , mmap_data(data)
        , mmap_size(size) {}
};
