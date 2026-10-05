module carven:test.internal.support.file;

import :support.file;
import :test.harness.directory;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Support file: writes and reads exact bytes"_test = [] static noexcept {
        const auto temporary = TempDirectory("carven-support-file");
        const auto path = temporary.path("bytes");
        const auto expected = std::string("alpha\0beta\n", 11);

        const auto written = write_file(path, expected);
        if (!expect(written.has_value())) {
            return;
        }
        auto input = std::ifstream(path, std::ios::binary);
        if (!expect(input.is_open())) {
            return;
        }
        const auto actual =
            std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        expect(!input.bad());
        expect_equal(actual, expected);

        const auto read_path = temporary.path("read-input");
        auto output = std::ofstream(read_path, std::ios::binary | std::ios::trunc);
        if (!expect(output.is_open())) {
            return;
        }
        output.write(expected.data(), static_cast<std::streamsize>(expected.size()));
        output.close();
        if (!expect(output.good())) {
            return;
        }
        const auto read = read_file(read_path, expected.size());
        if (!expect(read.has_value())) {
            return;
        }
        expect_equal(*read, expected);
    };

    "Support file: missing paths report the failed operation"_test = [] static noexcept {
        const auto temporary = TempDirectory("carven-support-file");

        const auto read = read_file(temporary.path("missing"), 1024);
        if (!expect(!read.has_value())) {
            return;
        }
        expect_equal(read.error().operation, FileOperation::Inspect);
        expect(read.error().code);
    };
});

} // namespace
