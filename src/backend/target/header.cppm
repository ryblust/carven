module carven:backend.target.header;

import std;

enum class TargetHeaderDelimiter { AngleBrackets, Quotes };
enum class TargetHeaderGroup {
    Associated,
    Runtime,
    Generated,
    StandardLibrary,
};

struct TargetHeader final {
    TargetHeaderDelimiter delimiter;
    std::string path;
    auto operator<=>(const TargetHeader&) const noexcept = default;
};

struct TargetHeaderProvider final {
    TargetHeaderGroup group;
    std::string_view path;
};

struct TargetHeaderRequirement final {
    TargetHeader header;
    TargetHeaderGroup group;
};
