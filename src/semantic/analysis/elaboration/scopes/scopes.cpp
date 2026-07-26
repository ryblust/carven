module carven:semantic.analysis.elaboration.scopes.impl;

import :semantic.analysis.elaboration.scopes;
import :support.invariant;
import std;

ScopeStack::ScopeStack(SemanticConstruction& builder) noexcept
    : semantic_builder(std::addressof(builder)) {}

auto ScopeStack::enter_scope() noexcept -> ScopeGuard {
    return ScopeGuard(*this);
}

auto ScopeStack::enter_lambda_boundary() noexcept -> LambdaBoundaryGuard {
    return LambdaBoundaryGuard(*this);
}

auto ScopeStack::bind(std::string_view name, SymbolID symbol) noexcept -> bool {
    if (scopes.empty()) {
        invariant_violation("binding requires an active scope");
    }
    auto& scope = scopes.back();
    if (!scope.bindings.emplace(name, symbol).second) {
        return false;
    }
    if (scope.semantic_scope.has_value()) {
        semantic_builder->bind_symbol(
            symbol,
            {
                .scope = *scope.semantic_scope,
                .declaration_order = scope.next_declaration_order,
            }
        );
        ++scope.next_declaration_order;
    }
    return true;
}

auto ScopeStack::current_scope() const noexcept -> SemanticScopeID {
    if (scopes.empty() || !scopes.back().semantic_scope.has_value()) {
        invariant_violation("structured semantic node requires an active lexical scope");
    }
    return *scopes.back().semantic_scope;
}

auto ScopeStack::find(std::string_view name) const noexcept -> ScopedSymbolLookup {
    auto first_visible_scope = 0uz;
    auto has_lambda_boundary = false;
    for (auto index = scopes.size(); index > 0; --index) {
        if (scopes[index - 1].lambda_boundary) {
            first_visible_scope = index - 1;
            has_lambda_boundary = true;
            break;
        }
    }
    for (auto index = scopes.size(); index > first_visible_scope; --index) {
        const auto found = scopes[index - 1].bindings.find(name);
        if (found != scopes[index - 1].bindings.end()) {
            return {.symbol = found->second, .requires_capture = false};
        }
    }
    if (has_lambda_boundary) {
        for (auto index = first_visible_scope; index > 0; --index) {
            const auto found = scopes[index - 1].bindings.find(name);
            if (found != scopes[index - 1].bindings.end()) {
                return {.symbol = found->second, .requires_capture = true};
            }
        }
    }
    return {.symbol = std::nullopt, .requires_capture = false};
}

auto ScopeStack::find_enclosing(std::string_view name) const noexcept -> std::optional<SymbolID> {
    if (scopes.empty()) {
        return std::nullopt;
    }
    for (auto index = scopes.size() - 1; index > 0; --index) {
        const auto found = scopes[index - 1].bindings.find(name);
        if (found != scopes[index - 1].bindings.end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

ScopeGuard::ScopeGuard(ScopeStack& value) noexcept
    : scopes(&value),
      depth(scopes->scopes.size()) {
    const auto parent = scopes->scopes.empty() ? std::optional<SemanticScopeID>()
                                               : scopes->scopes.back().semantic_scope;
    const auto semantic_scope = scopes->semantic_builder == nullptr
        ? std::optional<SemanticScopeID>()
        : scopes->semantic_builder->append_scope(parent);
    scopes->scopes.push_back({
        .bindings = {},
        .semantic_scope = semantic_scope,
        .next_declaration_order = 0,
        .lambda_boundary = false,
    });
}

ScopeGuard::~ScopeGuard() noexcept {
    if (scopes->scopes.size() != depth + 1) {
        invariant_violation("scope guard was destroyed out of scopes order");
    }
    scopes->scopes.pop_back();
}

LambdaBoundaryGuard::LambdaBoundaryGuard(ScopeStack& value) noexcept
    : scopes(&value),
      boundary(scopes->scopes.size()) {
    if (scopes->scopes.empty()) {
        invariant_violation("lambda boundary requires an active scope");
    }
    --boundary;
    if (scopes->scopes[boundary].lambda_boundary) {
        invariant_violation("scope already owns a lambda boundary");
    }
    scopes->scopes[boundary].lambda_boundary = true;
}

LambdaBoundaryGuard::~LambdaBoundaryGuard() noexcept {
    close();
}

auto LambdaBoundaryGuard::close() noexcept -> void {
    if (!active) {
        return;
    }
    if (boundary >= scopes->scopes.size() || !scopes->scopes[boundary].lambda_boundary) {
        invariant_violation("lambda boundary was closed out of scopes order");
    }
    scopes->scopes[boundary].lambda_boundary = false;
    active = false;
}
