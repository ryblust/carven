module carven:test.internal.support.file;

import :support.file;
import :test.harness.directory;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Support file: writes and reads exact bytes", [] static noexcept {
        const auto temporary = ct::TempDirectory("carven-support-file");
        const auto path = temporary.path("bytes");
        const auto expected = std::string("alpha\0beta\n", 11);

        const auto written = write_file(path, expected);
        if (!ct::expect(written.has_value())) {
            return;
        }
        auto input = std::ifstream(path, std::ios::binary);
        if (!ct::expect(input.is_open())) {
            return;
        }
        const auto actual =
            std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        ct::expect(!input.bad());
        ct::expect_equal(actual, expected);

        const auto read_path = temporary.path("read-input");
        auto output = std::ofstream(read_path, std::ios::binary | std::ios::trunc);
        if (!ct::expect(output.is_open())) {
            return;
        }
        output.write(expected.data(), static_cast<std::streamsize>(expected.size()));
        output.close();
        if (!ct::expect(output.good())) {
            return;
        }
        const auto read = read_file(read_path, expected.size());
        if (!ct::expect(read.has_value())) {
            return;
        }
        ct::expect_equal(*read, expected);
    });

    ct::test("Support file: missing paths report the failed operation", [] static noexcept {
        const auto temporary = ct::TempDirectory("carven-support-file");

        const auto read = read_file(temporary.path("missing"), 1024);
        if (!ct::expect(!read.has_value())) {
            return;
        }
        ct::expect_equal(read.error().operation, FileOperation::Inspect);
        ct::expect(read.error().code);
    });
});

} // namespace
