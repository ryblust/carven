#include <carven/api/consumer.hpp>

auto main() -> int {
    const auto observed = carven::api::consumer::observed();
    return observed.first == 7 && observed.second == 8 && observed.score == 84 ? 0 : 1;
}
