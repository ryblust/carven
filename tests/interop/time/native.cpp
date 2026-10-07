#include <carven/std/time/native.hpp>

auto time_native_header_contract() noexcept -> void {
    auto operation = carven_time_wait_until_nanoseconds(carven_time_now_nanoseconds());
    static_cast<void>(operation);
}
