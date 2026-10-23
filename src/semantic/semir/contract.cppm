module carven:semantic.semir.contract;

import :semantic.semir.ids;
import :semantic.semir.values;
import std;

auto native_type_name(const ExecutionValueAccess& values, TypeID type) noexcept
    -> std::optional<std::string>;

auto callable_contract_identity(
    const ExecutionValueAccess& values,
    CallableSignatureID signature
) noexcept -> std::optional<std::string>;

auto callable_contract_identity(
    const ExecutionValueAccess& values,
    const CallableSignature& signature
) noexcept -> std::optional<std::string>;
