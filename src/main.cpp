import zero.transpiler;
import std;

static constexpr auto SOURCE = std::string_view(R"(
    import std using { println }

    fn main() {
        println("Hello World!");
    }
)");

auto main() noexcept -> int {
    std::println("{}", transpile(SOURCE));
}