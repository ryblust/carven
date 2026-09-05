module carven:semantic.semir.ids;

import :semantic.semir.identity;
import std;

template<typename Value, typename ID>
class MutableProgramTable;
template<typename Value, typename ID>
class ReservedProgramTable;
template<typename Value, typename ID>
class MutableBodyTable;
template<typename Value, typename ID>
class ImmutableBodyTable;
template<typename ID, typename Value, typename Identity>
class IDTableEntries;

template<typename Tag>
class ProgramID final {
public:
    constexpr auto owner() const noexcept -> ProgramIdentity { return program_identity; }
    constexpr auto index() const noexcept -> std::uint32_t { return row_index; }
    constexpr auto operator<=>(const ProgramID&) const noexcept = default;

private:
    explicit constexpr ProgramID(ProgramIdentity owner, std::uint32_t index) noexcept
        : program_identity(owner),
          row_index(index) {}

    ProgramIdentity program_identity;
    std::uint32_t row_index;

    template<typename Value, typename ID>
    friend class MutableProgramTable;
    template<typename Value, typename ID>
    friend class ReservedProgramTable;
    template<typename ID, typename Value, typename Identity>
    friend class IDTableEntries;
};

template<typename Tag>
class BodyLocalID final {
public:
    constexpr auto owner() const noexcept -> BodyIdentity { return body_identity; }
    constexpr auto index() const noexcept -> std::uint32_t { return row_index; }
    constexpr auto operator<=>(const BodyLocalID&) const noexcept = default;

private:
    explicit constexpr BodyLocalID(BodyIdentity owner, std::uint32_t index) noexcept
        : body_identity(owner),
          row_index(index) {}

    BodyIdentity body_identity;
    std::uint32_t row_index;

    friend class BodyBuilder;

    template<typename Value, typename ID>
    friend class MutableBodyTable;
    template<typename ID, typename Value, typename Identity>
    friend class IDTableEntries;
    template<typename Value, typename ID>
    friend class ImmutableBodyTable;
};

struct ModuleIDTag final {};
struct FunctionIDTag final {};
struct StructIDTag final {};
struct EnumIDTag final {};
struct EnumCaseIDTag final {};
struct ModuleConstantIDTag final {};
struct CallableIDTag final {};
struct BodyIDTag final {};
struct TestIDTag final {};
struct TypeIDTag final {};
struct ConstantIDTag final {};
struct FailureSetIDTag final {};
struct CallableSignatureIDTag final {};
struct TypeTermIDTag final {};
struct FailureTermIDTag final {};

using ModuleID = ProgramID<ModuleIDTag>;
using FunctionID = ProgramID<FunctionIDTag>;
using StructID = ProgramID<StructIDTag>;
using EnumID = ProgramID<EnumIDTag>;
using EnumCaseID = ProgramID<EnumCaseIDTag>;
using ModuleConstantID = ProgramID<ModuleConstantIDTag>;
using CallableID = ProgramID<CallableIDTag>;
using BodyID = ProgramID<BodyIDTag>;
using TestID = ProgramID<TestIDTag>;
using TypeID = ProgramID<TypeIDTag>;
using ConstantID = ProgramID<ConstantIDTag>;
using FailureSetID = ProgramID<FailureSetIDTag>;
using CallableSignatureID = ProgramID<CallableSignatureIDTag>;
using TypeTermID = ProgramID<TypeTermIDTag>;
using FailureTermID = ProgramID<FailureTermIDTag>;

struct ScopeIDTag final {};
struct LifetimeRegionIDTag final {};
struct LocalBindingIDTag final {};
struct PatternIDTag final {};

using ScopeID = BodyLocalID<ScopeIDTag>;
using LifetimeRegionID = BodyLocalID<LifetimeRegionIDTag>;
using LocalBindingID = BodyLocalID<LocalBindingIDTag>;
using PatternID = BodyLocalID<PatternIDTag>;
