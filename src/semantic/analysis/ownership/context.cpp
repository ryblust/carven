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
    return std::tie(holder, backing) <=> std::tie(other.holder, other.backing);
}

auto OwnershipStorageLoan::operator==(const OwnershipStorageLoan& other) const noexcept -> bool {
    return (*this <=> other) == 0;
}

auto OwnershipObjectState::operator==(const OwnershipObjectState& other) const noexcept -> bool {
    return available == other.available && relationships == other.relationships;
}

auto OwnershipExternalObject::operator==(const OwnershipExternalObject& other) const noexcept
    -> bool {
    return type == other.type && state == other.state;
}
