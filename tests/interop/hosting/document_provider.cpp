#include <carven/providers/tests/interop/hosting/document_pair.hpp>

namespace carven::providers::tests::interop::hosting::document_pair::cv_escaped_functions {

auto alive::invoke(alive::Argument0 document) noexcept -> alive::Completion {
    return alive::service(document);
}

auto read::invoke(read::Argument0 document) noexcept -> read::Completion {
    return read::service(document);
}

auto publish::invoke(publish::Argument0 document, publish::Argument1 text) noexcept
    -> publish::Completion {
    return publish::service(document, text);
}

auto native_text::invoke() noexcept -> native_text::Completion {
    return native_text::service();
}

auto consume::invoke(consume::Argument0 text) noexcept -> consume::Completion {
    return consume::service(std::move(text));
}

auto inspect::invoke(inspect::Argument0 accepted) noexcept -> inspect::Completion {
    return inspect::service(accepted);
}

auto token::invoke() noexcept -> token::Completion {
    return token::service();
}

} // namespace carven::providers::tests::interop::hosting::document_pair::cv_escaped_functions
