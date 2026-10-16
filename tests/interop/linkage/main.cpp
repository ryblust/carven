#include <carven/api/tests/interop/linkage/app.hpp>
#include <carven/api/tests/interop/linkage/library.hpp>
#include <carven/api/tests/interop/linkage/second.hpp>

int main() {
    return carven::api::tests::interop::linkage::app::answer() == 42
            && carven::api::tests::interop::linkage::library::value() == 41
            && carven::api::tests::interop::linkage::second::value() == 73
            && carven::api::tests::interop::linkage::library::read(
                   carven::api::tests::interop::linkage::library::make()
               ) == 47
            && carven::api::tests::interop::linkage::second::read(
                   carven::api::tests::interop::linkage::second::make()
               ) == 79
        ? 0
        : 1;
}
