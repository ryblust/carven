#include <carven/api/tests/interop/linkage/app.hpp>
#include <carven/api/tests/interop/linkage/library.hpp>
#include <carven/api/tests/interop/linkage/second.hpp>

int main() {
    namespace library = carven::api::tests::interop::linkage::library;
    namespace second = carven::api::tests::interop::linkage::second;
    const auto first_value = library::value();
    const auto second_value = second::value();
    const auto first_record = library::make();
    const auto second_record = second::make();
    if (!first_value.success_if()
        || !second_value.success_if()
        || !first_record.success_if()
        || !second_record.success_if()) {
        return 1;
    }
    return carven::api::tests::interop::linkage::app::answer() == 42
            && first_value.success_if()->value == 41
            && second_value.success_if()->value == 73
            && library::read(first_record.success_if()->value) == 47
            && second::read(second_record.success_if()->value) == 79
        ? 0
        : 1;
}
