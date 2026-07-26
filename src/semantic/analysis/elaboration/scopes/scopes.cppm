module carven:semantic.analysis.elaboration.scopes;

import :semantic.analysis.builder;
import :semantic.hir.ids;
import std;

struct ScopedSymbolLookup final {
    std::optional<SymbolID> symbol;
    bool requires_capture;
};

class ScopeStack;

class ScopeGuard final {
public:
    ScopeGuard(const ScopeGuard&) = delete;
    auto operator=(const ScopeGuard&) -> ScopeGuard& = delete;
    ~ScopeGuard() noexcept;

private:
    explicit ScopeGuard(ScopeStack& scopes) noexcept;

    ScopeStack* scopes;
    std::size_t depth;

    friend class ScopeStack;
};

class LambdaBoundaryGuard final {
public:
    LambdaBoundaryGuard(const LambdaBoundaryGuard&) = delete;
    auto operator=(const LambdaBoundaryGuard&) -> LambdaBoundaryGuard& = delete;
    ~LambdaBoundaryGuard() noexcept;

    auto close() noexcept -> void;

private:
    explicit LambdaBoundaryGuard(ScopeStack& scopes) noexcept;

    ScopeStack* scopes;
    std::size_t boundary;
    bool active = true;

    friend class ScopeStack;
};

class ScopeStack final {
public:
    ScopeStack() = default;
    explicit ScopeStack(SemanticConstruction& builder) noexcept;

    auto enter_scope() noexcept -> ScopeGuard;
    auto enter_lambda_boundary() noexcept -> LambdaBoundaryGuard;
    auto bind(std::string_view name, SymbolID symbol) noexcept -> bool;
    auto find(std::string_view name) const noexcept -> ScopedSymbolLookup;
    auto find_enclosing(std::string_view name) const noexcept -> std::optional<SymbolID>;
    auto current_scope() const noexcept -> SemanticScopeID;

private:
    struct Scope final {
        std::flat_map<std::string, SymbolID, std::less<>> bindings;
        std::optional<SemanticScopeID> semantic_scope;
        std::uint32_t next_declaration_order;
        bool lambda_boundary;
    };

    std::vector<Scope> scopes;
    SemanticConstruction* semantic_builder = nullptr;

    friend class ScopeGuard;
    friend class LambdaBoundaryGuard;
};
