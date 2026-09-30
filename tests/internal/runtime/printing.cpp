module;
#include <carven/runtime/print.hpp>
#include <carven/runtime/report.hpp>

module carven:test.internal.runtime.printing;

import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Runtime printing: text arguments retain separators Unicode and NUL",
        [] static noexcept {
            const auto stream =
                std::unique_ptr<std::FILE, decltype(&std::fclose)>(std::tmpfile(), &std::fclose);
            if (!ct::expect(stream != nullptr)) {
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
            ct::expect(
                std::string_view(bytes.data(), size)
                == std::string_view("42 true 我 \0 raw\n", 18uz)
            );
        }
    );

    ct::test(
        "Runtime printing: structural display escapes text and bounds sequences and UTF8",
        [] static noexcept {
            auto writer = carven::runtime::DisplayWriter();
            writer.quoted(std::string_view("a\0\"\\\n", 5));
            ct::expect(writer.result() == "\"a\\0\\\"\\\\\\n\"");

            auto sequence = carven::runtime::DisplayWriter();
            const auto values = std::array<int, 65>();
            sequence.sequence(values, [](auto& output, int value) static noexcept {
                output.scalar(value);
            });
            ct::expect(sequence.result().starts_with("[\n    0,\n"));
            ct::expect(sequence.result().ends_with("\n    ...,\n]"));

            auto empty = carven::runtime::DisplayWriter();
            empty.sequence(std::array<int, 0> {}, [](auto& output, int value) static noexcept {
                output.scalar(value);
            });
            ct::expect(empty.result() == "[]");

            auto bounded = carven::runtime::DisplayWriter();
            bounded.text(std::string(16383, 'a'));
            bounded.text("我");
            ct::expect(bounded.result().size() == 16386uz);
            ct::expect(bounded.result().ends_with("a..."));
        }
    );
    ct::test("Runtime printing: writer depth bounds recursive emitters", [] static noexcept {
        struct Nested final {
            auto operator()(carven::runtime::DisplayWriter& writer, int remaining) const noexcept
                -> void {
                if (!writer.enter()) {
                    return;
                }
                writer.text("[");
                if (remaining != 0) {
                    (*this)(writer, remaining - 1);
                } else {
                    writer.scalar(1);
                }
                writer.text("]");
                writer.leave();
            }
        };
        auto writer = carven::runtime::DisplayWriter();
        Nested {}(writer, 10);
        ct::expect_equal(writer.result(), "[[[[[[[[...]]]]]]]]");
        writer.scalar(2);
        ct::expect(writer.result().ends_with("]2"));
    });
    ct::test(
        "Runtime reports: repeated operand text is omitted before consuming the report budget",
        [] static noexcept {
            const auto bytes = std::string(10'000uz, 'a');
            const auto source = std::format("\"{}\"", bytes);
            const auto left = std::string_view(bytes);
            const auto right = std::string_view("b");
            auto emissions = 0;
            const auto emit = [&](carven::runtime::DisplayWriter& output, auto value) noexcept {
                ++emissions;
                output.scalar(value);
            };
            auto comparisons = 0;
            const auto compare = [&](auto first, auto second) noexcept {
                ++comparisons;
                return first == second;
            };
            auto writer = carven::runtime::DisplayWriter();
            ct::expect(!carven::runtime::observe_comparison(
                writer,
                carven::runtime::structural_display(left, emit),
                carven::runtime::structural_display(right, emit),
                compare,
                source,
                "other"
            ));
            ct::expect_equal(writer.result(), "other: \"b\"\n");
            ct::expect_equal(comparisons, 1);
            ct::expect_equal(emissions, 2);
            auto passed = carven::runtime::DisplayWriter();
            ct::expect(
                carven::runtime::observe_comparison(
                    passed,
                    carven::runtime::structural_display(left, emit),
                    carven::runtime::structural_display(left, emit),
                    compare,
                    source,
                    source
                )
            );
            ct::expect(passed.result().empty());
            ct::expect_equal(comparisons, 2);
            ct::expect_equal(emissions, 2);
        }
    );
});

} // namespace
