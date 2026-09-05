module carven:semantic.semir.identity;

import :source.provenance.ids;
import std;

class ProgramDraft;
class BodyBuilder;

class ProgramIdentity final {
public:
    constexpr auto operator<=>(const ProgramIdentity&) const noexcept = default;

private:
    explicit constexpr ProgramIdentity(std::uint64_t value) noexcept
        : identity_value(value) {}

    static auto fresh() noexcept -> ProgramIdentity;

    std::uint64_t identity_value;

    friend class ProgramDraft;
};

class BodyIdentity final {
public:
    constexpr auto program() const noexcept -> ProgramIdentity { return program_identity; }
    constexpr auto body_index() const noexcept -> std::uint32_t { return body_index_value; }
    constexpr auto operator<=>(const BodyIdentity&) const noexcept = default;

private:
    explicit constexpr BodyIdentity(ProgramIdentity program, std::uint32_t body_index) noexcept
        : program_identity(program),
          body_index_value(body_index) {}

    ProgramIdentity program_identity;
    std::uint32_t body_index_value;

    friend class BodyBuilder;
    friend class ProgramDraft;
};
