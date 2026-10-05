module;
#include <carven/runtime/print.hpp>
#include <carven/runtime/range.hpp>
#include <carven/runtime/report.hpp>

module carven:test.internal.runtime.printing;

import :test.harness.framework;
import std;

namespace {

struct StatefulEmitter final {
    int offset;

    auto operator()(
        carven::runtime::DisplayWriter& writer,
        int value,
        std::size_t depth
    ) const noexcept -> void {
        writer.scalar(value + offset, depth);
    }
};

template<typename Emit>
concept SequenceEmitterType = requires { typename carven::runtime::SequenceDisplay<Emit>; };

template<typename Emit>
concept RangeEmitterType = requires { typename carven::runtime::RangeDisplay<Emit>; };

template<typename Emit>
concept StructuralEmitter = requires (Emit& emit) { carven::runtime::structural_display(0, emit); };

using IntRange = carven::runtime::Range<int>;
using ScalarEmitter = carven::runtime::ScalarDisplay;

static_assert(SequenceEmitterType<ScalarEmitter>);
static_assert(RangeEmitterType<ScalarEmitter>);
static_assert(!SequenceEmitterType<StatefulEmitter>);
static_assert(!RangeEmitterType<StatefulEmitter>);
static_assert(StructuralEmitter<StatefulEmitter>);
static_assert(!StructuralEmitter<decltype([](auto&, int) static noexcept {})>);

const TestSuite suite([] static noexcept {
    "Runtime printing: text arguments retain separators Unicode and NUL"_test = [] static noexcept {
        const auto stream =
            std::unique_ptr<std::FILE, decltype(&std::fclose)>(std::tmpfile(), &std::fclose);
        if (!expect(stream != nullptr)) {
            return;
        }
        carven::runtime::detail::print_values<true>(
            stream.get(),
            std::string_view("42"),
            std::string_view("true"),
            std::string_view("我"),
            std::string_view("\0", 1),
            std::string_view("raw")
        );
        std::rewind(stream.get());
        auto bytes = std::array<char, 64uz>();
        const auto size = std::fread(bytes.data(), 1uz, bytes.size(), stream.get());
        expect(
            std::string_view(bytes.data(), size) == std::string_view("42 true 我 \0 raw\n", 18uz)
        );
    };

    "Runtime printing: structural display escapes text and bounds sequences and UTF8"_test =
        [] static noexcept {
            auto writer = carven::runtime::DisplayWriter();
            writer.quoted(std::string_view("a\0\"\\\n", 5));
            expect(writer.result() == "\"a\\0\\\"\\\\\\n\"");

            auto sequence = carven::runtime::DisplayWriter();
            const auto values = std::array<int, 65>();
            sequence.sequence(values, carven::runtime::stateless_value<ScalarEmitter>, 0);
            expect(sequence.result().starts_with("[\n    0,\n"));
            expect(sequence.result().ends_with("\n    ...,\n]"));

            auto empty = carven::runtime::DisplayWriter();
            empty.sequence(
                std::array<int, 0> {},
                carven::runtime::stateless_value<ScalarEmitter>,
                0
            );
            expect(empty.result() == "[]");

            auto bounded = carven::runtime::DisplayWriter();
            bounded.text(std::string(16383, 'a'));
            bounded.text("我");
            expect(bounded.result().size() == 16386uz);
            expect(bounded.result().ends_with("a..."));
        };
    "Runtime display: explicit depth bounds each value independently"_test = [] static noexcept {
        auto writer = carven::runtime::DisplayWriter();
        writer.scalar(1, 7);
        writer.scalar(2, 8);
        writer.scalar(3, 9);
        writer.line(0);
        writer.scalar(4, 0);
        expect_equal(writer.result(), "1......\n4");

        auto calls = 0;
        const auto emit = [&](auto& output, int value, std::size_t depth) noexcept {
            ++calls;
            expect_equal(depth, 8uz);
            output.scalar(value, depth);
        };
        auto siblings = carven::runtime::DisplayWriter();
        siblings.range(IntRange {.first = 1, .last = 2, .inclusive = true}, emit, 7);
        siblings.range(IntRange {.first = 3, .last = 4, .inclusive = false}, emit, 8);
        siblings.scalar(5, 0);
        expect_equal(calls, 2);
        expect_equal(siblings.result(), ".....=......5");
    };
    "Runtime display: nested sequences indent independent siblings"_test = [] static noexcept {
        const auto nested = std::array {std::array {1, 2}, std::array {3, 4}};
        using Emit =
            carven::runtime::SequenceDisplay<carven::runtime::SequenceDisplay<ScalarEmitter>>;
        auto writer = carven::runtime::DisplayWriter();
        carven::runtime::stateless_value<Emit>(writer, nested, 0);
        writer.scalar(5, 0);
        expect_equal(
            writer.result(),
            "[\n    [\n        1,\n        2,\n    ],\n    [\n        3,\n        4,\n    ],\n]5"
        );
    };
    "Runtime display: callbacks borrow noncopyable state during consumption"_test =
        [] static noexcept {
            struct Emitter final {
                int calls;

                Emitter() noexcept
                    : calls(0) {}
                Emitter(const Emitter&) = delete;
                Emitter(Emitter&&) = delete;

                auto operator()(
                    carven::runtime::DisplayWriter& writer,
                    int value,
                    std::size_t depth
                ) & noexcept -> void {
                    ++calls;
                    writer.scalar(value + calls, depth);
                }
                auto operator()(carven::runtime::DisplayWriter&, int, std::size_t) && -> void =
                    delete;
            };
            struct Compare final {
                int calls;

                Compare() noexcept
                    : calls(0) {}
                Compare(const Compare&) = delete;
                Compare(Compare&&) = delete;

                auto operator()(int left, int right) & noexcept -> bool {
                    ++calls;
                    return left == right;
                }
                auto operator()(int, int) && -> bool = delete;
            };
            auto emit = Emitter();
            auto writer = carven::runtime::DisplayWriter();
            writer.sequence(std::array {1, 2}, emit, 0);
            writer.range(IntRange {.first = 3, .last = 4, .inclusive = false}, emit, 0);
            expect_equal(emit.calls, 4);
            expect_equal(writer.result(), "[\n    2,\n    4,\n]6..8");

            auto compare = Compare();
            auto report = carven::runtime::DisplayWriter();
            expect(!carven::runtime::observe_comparison(
                report,
                carven::runtime::structural_display(5, emit),
                carven::runtime::structural_display(6, emit),
                compare,
                "left",
                "right"
            ));
            expect_equal(compare.calls, 1);
            expect_equal(emit.calls, 6);
            expect_equal(report.result(), "left: 10\nright: 12\n");

            scenario("temporary callbacks are borrowed as lvalues", [] static noexcept {
                auto output = carven::runtime::DisplayWriter();
                output.sequence(std::array {1, 2}, Emitter(), 0);
                output.range(IntRange {.first = 3, .last = 4, .inclusive = false}, Emitter(), 0);
                expect_equal(output.result(), "[\n    2,\n    4,\n]4..6");

                auto explanation = carven::runtime::DisplayWriter();
                expect(!carven::runtime::observe_comparison(
                    explanation,
                    carven::runtime::structural_display(5, Emitter()),
                    carven::runtime::structural_display(6, Emitter()),
                    Compare(),
                    "left",
                    "right"
                ));
                expect_equal(explanation.result(), "left: 6\nright: 7\n");
            });

            const auto stream =
                std::unique_ptr<std::FILE, decltype(&std::fclose)>(std::tmpfile(), &std::fclose);
            if (!expect(stream != nullptr)) {
                return;
            }
            carven::runtime::detail::print_values<true>(
                stream.get(),
                carven::runtime::structural_display(7, Emitter())
            );
            std::rewind(stream.get());
            auto bytes = std::array<char, 8uz>();
            const auto size = std::fread(bytes.data(), 1uz, bytes.size(), stream.get());
            expect_equal(std::string_view(bytes.data(), size), "8\n");
        };
    "Runtime display: byte truncation stops sequence callbacks"_test = [] static noexcept {
        auto emissions = 0;
        const auto emit = [&](auto& output, int, std::size_t depth) noexcept {
            ++emissions;
            expect_equal(depth, 1uz);
            output.text(std::string(17000uz, 'a'));
        };
        auto writer = carven::runtime::DisplayWriter();
        writer.sequence(std::array {1, 2}, emit, 0);
        expect_equal(emissions, 1);
        expect(writer.result().ends_with("..."));
    };
    "Runtime reports: repeated operand text is omitted before consuming the report budget"_test =
        [] static noexcept {
            const auto bytes = std::string(10'000uz, 'a');
            const auto source = std::format("\"{}\"", bytes);
            const auto left = std::string_view(bytes);
            const auto right = std::string_view("b");
            auto emissions = 0;
            const auto emit = [&](carven::runtime::DisplayWriter& output,
                                  auto value,
                                  std::size_t depth) noexcept {
                ++emissions;
                output.scalar(value, depth);
            };
            auto comparisons = 0;
            const auto compare = [&](auto first, auto second) noexcept {
                ++comparisons;
                return first == second;
            };
            auto writer = carven::runtime::DisplayWriter();
            expect(!carven::runtime::observe_comparison(
                writer,
                carven::runtime::structural_display(left, emit),
                carven::runtime::structural_display(right, emit),
                compare,
                source,
                "other"
            ));
            expect_equal(writer.result(), "other: \"b\"\n");
            expect_equal(comparisons, 1);
            expect_equal(emissions, 2);
            auto passed = carven::runtime::DisplayWriter();
            expect(
                carven::runtime::observe_comparison(
                    passed,
                    carven::runtime::structural_display(left, emit),
                    carven::runtime::structural_display(left, emit),
                    compare,
                    source,
                    source
                )
            );
            expect(passed.result().empty());
            expect_equal(comparisons, 2);
            expect_equal(emissions, 2);
        };
});

} // namespace
