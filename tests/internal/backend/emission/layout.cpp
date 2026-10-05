module carven:test.internal.backend.emission.layout;

import :backend.emission.layout;
import :test.harness.framework;
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

const TestSuite suite([] static noexcept {
    "Layout: empty documents and mandatory lines have canonical endings"_test = [] static noexcept {
        auto empty = LayoutBuilder {};
        const auto empty_root = empty.empty();
        expect_equal(finish(std::move(empty), empty_root), std::string_view("\n"));

        auto lines = LayoutBuilder {};
        const auto root = lines.concat(
            {lines.text("first "), lines.line(), lines.line(), lines.text("second\t"), lines.line()}
        );
        expect_equal(finish(std::move(lines), root), std::string_view("first\n\nsecond\n"));
    };

    "Layout: text nodes own string-view input"_test = [] static noexcept {
        auto builder = LayoutBuilder {};
        auto source = std::string("stable");
        const auto root = builder.text(source);
        source.assign("changed");

        expect_equal(finish(std::move(builder), root), std::string_view("stable\n"));
    };

    "Layout: ordered choices use the first fitting alternative"_test = [] static noexcept {
        auto exact = LayoutBuilder {};
        const auto exact_root = choose(
            exact,
            {exact.text("alpha beta"),
             exact.concat({exact.text("alpha"), exact.line(), exact.text("beta")})}
        );
        expect_equal(finish(std::move(exact), exact_root, 10), std::string_view("alpha beta\n"));

        auto fallback = LayoutBuilder {};
        const auto fallback_root = choose(
            fallback,
            {fallback.text("alpha beta"),
             fallback.concat({fallback.text("alpha"), fallback.line(), fallback.text("beta")})}
        );
        expect_equal(
            finish(std::move(fallback), fallback_root, 9),
            std::string_view("alpha\nbeta\n")
        );
    };

    "Layout: nested choices remain independent unless flattened"_test = [] static noexcept {
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
        expect_equal(
            finish(std::move(independent), root, 9),
            std::string_view("head long\nvalue\n")
        );

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
        expect_equal(
            finish(std::move(flattened), flattened_root, 9),
            std::string_view("head\nlong\nvalue\n")
        );
    };

    "Layout: choices account for pending siblings and continuation indentation"_test =
        [] static noexcept {
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
            expect_equal(
                finish(std::move(builder), root, 14),
                std::string_view("alpha\n    beta gamma\n")
            );

            auto nested = LayoutBuilder {};
            const auto nested_choice = choose(
                nested,
                {nested.text("alpha beta"),
                 nested.concat({nested.text("alpha"), nested.line(), nested.text("beta")})}
            );
            const auto nested_root = nested.indent(4, nested_choice);
            expect_equal(
                finish(std::move(nested), nested_root, 10),
                std::string_view("    alpha\n    beta\n")
            );
        };

    "Layout: choices measure the indentation that their alternatives actually emit"_test =
        [] static noexcept {
            auto indented = LayoutBuilder {};
            const auto indented_root = choose(
                indented,
                {indented.indent(4, indented.text("123456")), indented.text("ok")}
            );
            expect_equal(finish(std::move(indented), indented_root, 6), std::string_view("ok\n"));

            auto reset = LayoutBuilder {};
            const auto reset_choice =
                choose(reset, {reset.reset_indent(reset.text("123456")), reset.text("ok")});
            const auto reset_root = reset.indent(4, reset_choice);
            expect_equal(finish(std::move(reset), reset_root, 6), std::string_view("123456\n"));

            auto raw = LayoutBuilder {};
            const auto raw_choice = choose(raw, {raw.raw("123456"), raw.text("ok")});
            const auto raw_root = raw.indent(4, raw_choice);
            expect_equal(finish(std::move(raw), raw_root, 6), std::string_view("123456\n"));
        };

    "Layout: indivisible tokens may exceed the configured width"_test = [] static noexcept {
        auto builder = LayoutBuilder {};
        const auto root = builder.text("indivisible");
        expect_equal(finish(std::move(builder), root, 4), std::string_view("indivisible\n"));
    };

    "Layout: raw fragments participate in fitting without changing bytes"_test =
        [] static noexcept {
            auto measured = LayoutBuilder {};
            const auto measured_root = choose(
                measured,
                {measured.concat({measured.text("x"), measured.raw("ab\nraw tail")}),
                 measured.text("fallback")}
            );
            expect_equal(
                finish(std::move(measured), measured_root, 3),
                std::string_view("xab\nraw tail\n")
            );

            auto preserved = LayoutBuilder {};
            const auto preserved_root = preserved.raw("raw \nnext\t");
            expect_equal(
                finish(std::move(preserved), preserved_root),
                std::string_view("raw \nnext\t\n")
            );
        };

    "Layout: reset indent places directives at column zero"_test = [] static noexcept {
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
        expect_equal(
            finish(std::move(builder), nested),
            std::string_view("    body\n#line 1\n    tail\n")
        );
    };

    "Layout: independent lines keep local choices across a wide continuation"_test =
        [] static noexcept {
            auto builder = LayoutBuilder();
            auto rows = std::vector<LayoutNodeID>();
            auto expected = std::string();
            for (auto index = 0uz; index < 4096uz; ++index) {
                rows.push_back(choose(builder, {builder.text("wide value"), builder.text("x")}));
                rows.push_back(builder.line());
                expected += "x\n";
            }
            const auto root = builder.concat(std::move(rows));
            expect_equal(finish(std::move(builder), root, 1uz), expected);
        };

    "Layout: deep nesting keeps indentation bounded by the line width"_test = [] static noexcept {
        auto builder = LayoutBuilder();
        auto root = builder.text("value");
        constexpr auto depth = 2000uz;
        for (auto index = 0uz; index < depth; ++index) {
            root = builder.concat(
                {builder.text("("),
                 builder.indent(4, builder.concat({builder.line(), root})),
                 builder.line(),
                 builder.text(")")}
            );
        }
        const auto rendered = finish(std::move(builder), root, 100);
        expect_less(rendered.size(), depth * 110uz);
        for (const auto line : rendered | std::views::split('\n')) {
            expect_less_equal(std::ranges::distance(line), 55);
        }
    };
});

} // namespace
