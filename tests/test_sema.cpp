#include "test_helpers.h"

import carven.frontend.lexer;
import carven.frontend.parser;
import carven.frontend.sema;
import std;

static auto sema_errors(std::string_view source) noexcept -> std::vector<ParseError> {
    auto result = parse(tokenize(source), source);
    if (!result.errors.empty()) return std::move(result.errors);
    return analyze(result.items, source);
}

TEST_CASE("Sema: if expression") {
    SUBCASE("value if requires else") {
        const auto errors = sema_errors("fn main() { let x = if true { 1 }; }");
        CHECK(!errors.empty());
    }

    SUBCASE("value if requires final expression without semicolon") {
        const auto errors = sema_errors("fn main() { let x = if true { 1; } else { 2 }; }");
        CHECK(!errors.empty());
    }

    SUBCASE("value if permits statements before final expression") {
        const auto errors = sema_errors("fn main() { let x = if true { work(); 1 } else { other(); 2 }; }");
        CHECK(errors.empty());
    }

    SUBCASE("value if rejects explicit return") {
        const auto errors = sema_errors("fn main() { let x = if true { return 1; } else { 2 }; }");
        CHECK(!errors.empty());
    }

    SUBCASE("statement if may omit else") {
        const auto errors = sema_errors("fn main() { if true { return; } }");
        CHECK(errors.empty());
    }
}

TEST_CASE("Sema: match expression") {
    SUBCASE("value match requires wildcard") {
        const auto errors = sema_errors("fn main() { let x = match a { 1 => { 2 } }; }");
        CHECK(!errors.empty());
    }

    SUBCASE("wildcard arm must be last") {
        const auto errors = sema_errors("fn main() { match a { _ => { b; } 1 => { c; } } }");
        CHECK(!errors.empty());
    }

    SUBCASE("value match requires final expression without semicolon") {
        const auto errors = sema_errors("fn main() { let x = match a { 1 => { 2; } _ => { 0 } }; }");
        CHECK(!errors.empty());
    }

    SUBCASE("value match rejects explicit return") {
        const auto errors = sema_errors("fn main() { let x = match a { 1 => { return 2; } _ => { 0 } }; }");
        CHECK(!errors.empty());
    }

    SUBCASE("statement match may omit wildcard") {
        const auto errors = sema_errors("fn main() { match a { 1 => { return; } } }");
        CHECK(errors.empty());
    }
}

TEST_CASE("Sema: reserved identifiers") {
    SUBCASE("__carven prefix is reserved") {
        const auto errors = sema_errors("fn __carven_main() { }");
        CHECK(!errors.empty());
    }
}

TEST_CASE("Sema: main args") {
    SUBCASE("main may accept one untyped parameter") {
        const auto errors = sema_errors("fn main(args) { }");
        CHECK(errors.empty());
    }

    SUBCASE("main rejects typed parameter") {
        const auto errors = sema_errors("fn main(args: i32) { }");
        CHECK(!errors.empty());
    }

    SUBCASE("main rejects multiple parameters") {
        const auto errors = sema_errors("fn main(args, extra) { }");
        CHECK(!errors.empty());
    }
}
