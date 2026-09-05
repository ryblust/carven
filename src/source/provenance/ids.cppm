module carven:source.provenance.ids;

import std;

class CompilationProvenanceStorage;

class ProvenanceIdentity final {
public:
    constexpr auto value() const noexcept -> std::uint64_t { return identity_value; }
    constexpr auto operator<=>(const ProvenanceIdentity&) const noexcept = default;

private:
    explicit constexpr ProvenanceIdentity(std::uint64_t value) noexcept
        : identity_value(value) {}

    static auto fresh() noexcept -> ProvenanceIdentity;

    std::uint64_t identity_value;

    friend class CompilationProvenanceStorage;
};

template<typename Tag>
class ProvenanceID final {
public:
    constexpr auto owner() const noexcept -> ProvenanceIdentity { return provenance_identity; }
    constexpr auto index() const noexcept -> std::uint32_t { return row_index; }
    constexpr auto operator<=>(const ProvenanceID&) const noexcept = default;

private:
    explicit constexpr ProvenanceID(ProvenanceIdentity owner, std::uint32_t index) noexcept
        : provenance_identity(owner),
          row_index(index) {}

    ProvenanceIdentity provenance_identity;
    std::uint32_t row_index;

    friend class CompilationProvenanceStorage;
};

struct ProgramSourceIDTag final {};
struct ProgramModuleIDTag final {};
struct ProgramSpellingIDTag final {};
struct ProgramOriginIDTag final {};

using ProgramSourceID = ProvenanceID<ProgramSourceIDTag>;
using ProgramModuleID = ProvenanceID<ProgramModuleIDTag>;
using ProgramSpellingID = ProvenanceID<ProgramSpellingIDTag>;
using ProgramOriginID = ProvenanceID<ProgramOriginIDTag>;
