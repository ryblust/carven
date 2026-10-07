module carven:semantic.analysis.ownership.context.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.ownership.context;
import :semantic.analysis.ownership;
import :semantic.semir.contents;
import :semantic.semir.program;
import :semantic.semir.traversal;
import :support.invariant;
import :support.visit;
import std;

namespace {

const auto empty_relationship_rows = OwnershipRelationshipRows {};

auto empty_rows(const OwnershipRelationshipRows& rows) noexcept -> bool {
    return rows.callable_loans.empty() && rows.captures.empty() && rows.storage_loans.empty();
}

} // namespace

OwnershipRelationships::OwnershipRelationships(OwnershipRelationshipRows value) noexcept
    : rows(
          empty_rows(value) ? nullptr
                            : std::make_unique<OwnershipRelationshipRows>(std::move(value))
      ) {}

OwnershipRelationships::OwnershipRelationships(const OwnershipRelationships& other) noexcept
    : rows(other.empty() ? nullptr : std::make_unique<OwnershipRelationshipRows>(*other.rows)) {}

auto OwnershipRelationships::operator=(const OwnershipRelationships& other) noexcept
    -> OwnershipRelationships& {
    if (this != &other) {
        if (other.empty()) {
            rows.reset();
        } else if (rows) {
            *rows = *other.rows;
        } else {
            rows = std::make_unique<OwnershipRelationshipRows>(*other.rows);
        }
    }
    return *this;
}

auto OwnershipRelationships::view() const noexcept -> const OwnershipRelationshipRows& {
    return rows ? *rows : empty_relationship_rows;
}

auto OwnershipRelationships::edit_existing() noexcept -> OwnershipRelationshipRows* {
    return rows.get();
}

auto OwnershipRelationships::edit() noexcept -> OwnershipRelationshipRows& {
    if (!rows) {
        rows = std::make_unique<OwnershipRelationshipRows>();
    }
    return *rows;
}

auto OwnershipRelationships::empty() const noexcept -> bool {
    return !rows || empty_rows(*rows);
}

auto OwnershipRelationships::operator==(const OwnershipRelationships& other) const noexcept
    -> bool {
    return view() == other.view();
}

auto OwnershipCallableLoan::operator<=>(const OwnershipCallableLoan& other) const noexcept
    -> std::strong_ordering {
    return std::tie(holder, backing, callable, direct_only)
        <=> std::tie(other.holder, other.backing, other.callable, other.direct_only);
}

auto OwnershipCallableLoan::operator==(const OwnershipCallableLoan& other) const noexcept -> bool {
    return (*this <=> other) == 0;
}

auto OwnershipCapture::operator<=>(const OwnershipCapture& other) const noexcept
    -> std::strong_ordering {
    return std::tie(holder, target) <=> std::tie(other.holder, other.target);
}

auto OwnershipCapture::operator==(const OwnershipCapture& other) const noexcept -> bool {
    return (*this <=> other) == 0;
}

auto OwnershipStorageLoan::operator<=>(const OwnershipStorageLoan& other) const noexcept
    -> std::strong_ordering {
    return std::tie(holder, backing, protection)
        <=> std::tie(other.holder, other.backing, other.protection);
}

auto OwnershipStorageLoan::operator==(const OwnershipStorageLoan& other) const noexcept -> bool {
    return (*this <=> other) == 0;
}

auto OwnershipObjectState::operator==(const OwnershipObjectState& other) const noexcept -> bool {
    return available == other.available
        && relationships == other.relationships
        && modified == other.modified
        && child_intent == other.child_intent;
}

auto OwnershipExternalObject::operator==(const OwnershipExternalObject& other) const noexcept
    -> bool {
    return type == other.type && state == other.state && site == other.site && many == other.many;
}
