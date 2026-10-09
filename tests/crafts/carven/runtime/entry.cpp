#include <carven/runtime/entry.hpp>

#include <type_traits>

static_assert(
    std::is_same_v<carven::runtime::EntryArgs, decltype(carven::runtime::entry_args(0, nullptr))>
);

auto entry_header_contract(int argc, const char* const* argv) noexcept -> std::size_t {
    auto count = std::size_t {0};
    const carven::runtime::EntryArgs arguments = carven::runtime::entry_args(argc, argv);
    for (const auto& [index, value] : arguments) {
        count += index + value.size();
    }
    return count;
}

auto entry_emitter_contract() noexcept -> void {
    struct Borrowed final {
        Borrowed() = default;
        Borrowed(const Borrowed&) = delete;
        Borrowed(Borrowed&&) = delete;

        auto operator()(
            carven::runtime::DisplayWriter& writer,
            const int& value,
            std::size_t depth
        ) & noexcept -> void {
            writer.scalar(value, depth);
        }

        auto operator()(carven::runtime::DisplayWriter&, const int&, std::size_t) && -> void =
            delete;
    };

    auto emit = Borrowed();
    const int* failure = nullptr;
    carven::runtime::report_entry_failure(
        failure,
        "failure",
        emit,
        carven::runtime::SourceSite::native()
    );
    carven::runtime::report_entry_failure(
        failure,
        "failure",
        Borrowed(),
        carven::runtime::SourceSite::native()
    );
    carven::runtime::report_entry_failure(
        failure,
        "failure",
        carven::runtime::stateless_value<carven::runtime::ScalarDisplay>,
        carven::runtime::SourceSite::native()
    );
}
