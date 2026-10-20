#pragma once

#include <carven/runtime/hosting/object.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace documents {

struct Token final {
    std::int32_t value;

    Token() = delete;
    explicit Token(std::int32_t value) noexcept;
    auto is_stopped() const noexcept -> bool;
};

inline Token::Token(std::int32_t value) noexcept
    : value(value) {}

inline auto Token::is_stopped() const noexcept -> bool {
    return true;
}

class Document;
using Ref = carven::runtime::hosting::ObjectRef<Document>;

namespace alternate {

using Ref = documents::Ref;

} // namespace alternate

class Document final {
public:
    explicit Document(std::string text) noexcept;
    Document(const Document&) = delete;
    auto operator=(const Document&) -> Document& = delete;
    auto read() const noexcept -> std::string;
    auto publish(const std::string& text) noexcept -> bool;
    auto edit(std::string text) noexcept -> void;
    auto rendered() const noexcept -> std::string_view;
    auto reference() const noexcept -> Ref;

private:
    std::string text;
    std::string view;
    // Invalidated before native text/view destruction.
    carven::runtime::hosting::Object registration;
};

inline Document::Document(std::string text) noexcept
    : text(std::move(text)),
      view(),
      registration(*this) {}

inline auto Document::read() const noexcept -> std::string {
    return text;
}

inline auto Document::publish(const std::string& text) noexcept -> bool {
    view = text;
    return true;
}

inline auto Document::edit(std::string value) noexcept -> void {
    text = std::move(value);
}

inline auto Document::rendered() const noexcept -> std::string_view {
    return view;
}

inline auto Document::reference() const noexcept -> Ref {
    return registration.reference<Document>();
}

} // namespace documents
