module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.emission.layout;

import :backend.emission.layout;
import std;

namespace {

auto choose(LayoutBuilder& builder, std::initializer_list<LayoutNodeID> alternatives) noexcept
    -> LayoutNodeID {
    return builder.choice(std::span<const LayoutNodeID>(alternatives.begin(), alternatives.size()));
}

auto finish(LayoutBuilder builder, LayoutNodeID root, std::size_t width = 100) noexcept
    -> std::string {
    return render_layout(std::move(builder).finish(root), width);
}

} // namespace

TEST_CASE("Layout: empty documents and mandatory lines have canonical endings") {
    auto empty = LayoutBuilder {};
    const auto empty_root = empty.empty();
    CHECK_EQ(finish(std::move(empty), empty_root), "\n");

    auto lines = LayoutBuilder {};
    const auto root = lines.concat(
        {lines.text("first "), lines.line(), lines.line(), lines.text("second\t"), lines.line()}
    );
    CHECK_EQ(finish(std::move(lines), root), "first\n\nsecond\n");
}

TEST_CASE("Layout: text nodes own string-view input") {
    auto builder = LayoutBuilder {};
    auto source = std::string("stable");
    const auto root = builder.text(source);
    source.assign("changed");

    CHECK_EQ(finish(std::move(builder), root), "stable\n");
}

TEST_CASE("Layout: ordered choices use the first fitting alternative") {
    auto exact = LayoutBuilder {};
    const auto exact_root = choose(
        exact,
        {exact.text("alpha beta"),
         exact.concat({exact.text("alpha"), exact.line(), exact.text("beta")})}
    );
    CHECK_EQ(finish(std::move(exact), exact_root, 10), "alpha beta\n");

    auto fallback = LayoutBuilder {};
    const auto fallback_root = choose(
        fallback,
        {fallback.text("alpha beta"),
         fallback.concat({fallback.text("alpha"), fallback.line(), fallback.text("beta")})}
    );
    CHECK_EQ(finish(std::move(fallback), fallback_root, 9), "alpha\nbeta\n");
}

TEST_CASE("Layout: nested choices remain independent unless flattened") {
    auto independent = LayoutBuilder {};
    const auto inner = choose(
        independent,
        {independent.text("long value"),
         independent.concat(
             {independent.text("long"), independent.line(), independent.text("value")}
         )}
    );
    const auto root = choose(
        independent,
        {independent.concat({independent.text("head "), inner}),
         independent.concat({independent.text("head"), independent.line(), inner})}
    );
    CHECK_EQ(finish(std::move(independent), root, 9), "head long\nvalue\n");

    auto flattened = LayoutBuilder {};
    const auto flattened_inner = choose(
        flattened,
        {flattened.text("long value"),
         flattened.concat({flattened.text("long"), flattened.line(), flattened.text("value")})}
    );
    const auto flattened_root = choose(
        flattened,
        {flattened.concat({flattened.text("head "), flattened.flatten(flattened_inner)}),
         flattened.concat({flattened.text("head"), flattened.line(), flattened_inner})}
    );
    CHECK_EQ(finish(std::move(flattened), flattened_root, 9), "head\nlong\nvalue\n");
}

TEST_CASE("Layout: choices account for pending siblings and continuation indentation") {
    auto builder = LayoutBuilder {};
    const auto body = choose(
        builder,
        {builder.text("alpha beta"),
         builder.concat(
             {builder.text("alpha"),
              builder.indent(4, builder.concat({builder.line(), builder.text("beta")}))}
         )}
    );
    const auto root = builder.concat({body, builder.text(" gamma")});
    CHECK_EQ(finish(std::move(builder), root, 14), "alpha\n    beta gamma\n");

    auto nested = LayoutBuilder {};
    const auto nested_choice = choose(
        nested,
        {nested.text("alpha beta"),
         nested.concat({nested.text("alpha"), nested.line(), nested.text("beta")})}
    );
    const auto nested_root = nested.indent(4, nested_choice);
    CHECK_EQ(finish(std::move(nested), nested_root, 10), "    alpha\n    beta\n");
}

TEST_CASE("Layout: choices measure the indentation that their alternatives actually emit") {
    auto indented = LayoutBuilder {};
    const auto indented_root =
        choose(indented, {indented.indent(4, indented.text("123456")), indented.text("ok")});
    CHECK_EQ(finish(std::move(indented), indented_root, 6), "ok\n");

    auto reset = LayoutBuilder {};
    const auto reset_choice =
        choose(reset, {reset.reset_indent(reset.text("123456")), reset.text("ok")});
    const auto reset_root = reset.indent(4, reset_choice);
    CHECK_EQ(finish(std::move(reset), reset_root, 6), "123456\n");

    auto raw = LayoutBuilder {};
    const auto raw_choice = choose(raw, {raw.raw("123456"), raw.text("ok")});
    const auto raw_root = raw.indent(4, raw_choice);
    CHECK_EQ(finish(std::move(raw), raw_root, 6), "123456\n");
}

TEST_CASE("Layout: indivisible tokens may exceed the configured width") {
    auto builder = LayoutBuilder {};
    const auto root = builder.text("indivisible");
    CHECK_EQ(finish(std::move(builder), root, 4), "indivisible\n");
}

TEST_CASE("Layout: raw fragments participate in fitting without changing bytes") {
    auto measured = LayoutBuilder {};
    const auto measured_root = choose(
        measured,
        {measured.concat({measured.text("x"), measured.raw("ab\nraw tail")}),
         measured.text("fallback")}
    );
    CHECK_EQ(finish(std::move(measured), measured_root, 3), "xab\nraw tail\n");

    auto preserved = LayoutBuilder {};
    const auto preserved_root = preserved.raw("raw \nnext\t");
    CHECK_EQ(finish(std::move(preserved), preserved_root), "raw \nnext\t\n");
}

TEST_CASE("Layout: reset indent places directives at column zero") {
    auto builder = LayoutBuilder {};
    const auto nested = builder.indent(
        4,
        builder.concat(
            {builder.text("body"),
             builder.line(),
             builder.reset_indent(builder.text("#line 1")),
             builder.line(),
             builder.text("tail")}
        )
    );
    CHECK_EQ(finish(std::move(builder), nested), "    body\n#line 1\n    tail\n");
}
