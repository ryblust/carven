import zero.driver.pipeline;
import zero.common.source;
import zero.tests.utils;
import std;

#define CHECK(expr)     check((expr), #expr)
#define CHECK_EQ(a, e)  check_eq((a), (e), #a, #e)

static auto test_transpile_empty_function() noexcept -> void {
    current_test = "transpile empty function";
    auto driver = Driver();
    const auto result = transpile(driver, {.filename = "test.zero", .content = "fn main() {}"});
    CHECK(!result.has_errors());
    CHECK(result.output.contains("auto main() noexcept -> int"));
}

static auto test_transpile_parse_error() noexcept -> void {
    current_test = "transpile parse error";
    auto driver = Driver();
    const auto result = transpile(driver, {.filename = "test.zero", .content = "!!! not valid"});
    CHECK(result.has_errors());
    CHECK(result.output.empty());
}

static auto test_transpile_with_params_and_return() noexcept -> void {
    current_test = "transpile with params and return";
    auto driver = Driver();
    const auto result = transpile(driver, {
        .filename = "test.zero",
        .content = "fn add(a: i32, b: i32) -> i32 {}"
    });
    CHECK(!result.has_errors());
    CHECK(result.output.contains("auto add(std::int32_t a, std::int32_t b) noexcept -> std::int32_t"));
}

static auto test_transpile_import() noexcept -> void {
    current_test = "transpile import";
    auto driver = Driver();
    const auto result = transpile(driver, {
        .filename = "test.zero",
        .content = "import std;"
    });
    CHECK(!result.has_errors());
    CHECK(!result.output.contains("import std;"));
}

static auto test_transpile_import_with_using() noexcept -> void {
    current_test = "transpile import with using";
    auto driver = Driver();
    const auto result = transpile(driver, {
        .filename = "test.zero",
        .content = "import std using { println, format };"
    });
    CHECK(!result.has_errors());
    CHECK(result.output.contains("using std::println;"));
    CHECK(result.output.contains("using std::format;"));
}

static auto test_parse_flags_defaults() noexcept -> void {
    current_test = "parse_flags defaults";
    auto driver = Driver();
    CHECK_EQ(driver.language_standard, static_cast<std::uint8_t>(23));
    CHECK_EQ(driver.import_std, false);
    CHECK(driver.input_files.empty());
}

static auto test_parse_flags_standard() noexcept -> void {
    current_test = "parse_flags standard";
    auto driver = Driver();
    const auto ok = driver.parse_flags({"-std=c++26", "file.zero"});
    CHECK(ok);
    CHECK_EQ(driver.language_standard, static_cast<std::uint8_t>(26));
}

static auto test_parse_flags_output_dir() noexcept -> void {
    current_test = "parse_flags output dir";
    auto driver = Driver();
    driver.parse_flags({"--output-dir=/tmp/out", "file.zero"});
    CHECK_EQ(driver.output_dir.string(), std::string("/tmp/out"));
}

static auto test_parse_flags_import_std() noexcept -> void {
    current_test = "parse_flags import std";
    auto driver = Driver();
    driver.parse_flags({"--import-std", "file.zero"});
    CHECK_EQ(driver.import_std, true);
}

static auto test_parse_flags_unknown_standard() noexcept -> void {
    current_test = "parse_flags unknown standard";
    auto driver = Driver();
    const auto ok = driver.parse_flags({"-std=c++99", "file.zero"});
    CHECK(!ok);
}

static auto test_parse_flags_input_files() noexcept -> void {
    current_test = "parse_flags input files";
    auto driver = Driver();
    driver.parse_flags({"a.zero", "b.zero"});
    CHECK_EQ(driver.input_files.size(), 2uz);
    CHECK_EQ(driver.input_files[0], "a.zero");
    CHECK_EQ(driver.input_files[1], "b.zero");
}

auto main() noexcept -> int {
    test_transpile_empty_function();
    test_transpile_parse_error();
    test_transpile_with_params_and_return();
    test_transpile_import();
    test_transpile_import_with_using();
    test_parse_flags_defaults();
    test_parse_flags_standard();
    test_parse_flags_output_dir();
    test_parse_flags_import_std();
    test_parse_flags_unknown_standard();
    test_parse_flags_input_files();

    return print_test_result();
}
