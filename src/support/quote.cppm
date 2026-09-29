module carven:support.quote;

import std;

// Quotes text for display, escaping control bytes and invalid UTF-8.
auto quote_text(std::string_view value) noexcept -> std::string;
