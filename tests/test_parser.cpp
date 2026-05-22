#include "doctest.h"

import carven.common.source;
import carven.frontend.token;
import carven.frontend.lexer;
import carven.frontend.ast;
import carven.frontend.parser;
import std;

TEST_CASE("Testing Parser") {
    static constexpr auto source = "import std;";
    const auto tokens = tokenize(source);
    const auto result = parse(tokens, source);
    const auto import_module = std::get_if<ImportItem>(&result.items[0]);
    CHECK_EQ(text_at(source, import_module->module_name), "std");
}
