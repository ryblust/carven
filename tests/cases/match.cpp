#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <barrier>
#include <bit>
#include <bitset>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <cfenv>
#include <cfloat>
#include <charconv>
#include <chrono>
#include <cinttypes>
#include <climits>
#include <clocale>
#include <cmath>
#include <compare>
#include <complex>
#include <concepts>
#include <condition_variable>
#include <coroutine>
#include <csetjmp>
#include <csignal>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cuchar>
#include <cwchar>
#include <cwctype>
#include <deque>
#include <exception>
#include <execution>
#include <expected>
#include <filesystem>
#include <format>
#include <forward_list>
#include <fstream>
#include <functional>
#include <future>
#include <initializer_list>
#include <iomanip>
#include <ios>
#include <iosfwd>
#include <iostream>
#include <istream>
#include <iterator>
#include <limits>
#include <list>
#include <locale>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <numbers>
#include <numeric>
#include <optional>
#include <ostream>
#include <print>
#include <queue>
#include <random>
#include <ranges>
#include <ratio>
#include <regex>
#include <scoped_allocator>
#include <semaphore>
#include <set>
#include <shared_mutex>
#include <source_location>
#include <span>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <stop_token>
#include <streambuf>
#include <string>
#include <string_view>
#include <syncstream>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <valarray>
#include <variant>
#include <vector>
#include <version>
#include <flat_map>
#include <flat_set>
#include <mdspan>



auto classify(auto value) noexcept {
    return ([&]() noexcept {
        auto&& __carven_match_0 = value;
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(__carven_match_0)>, std::int32_t>) {
            return "int";
        }
         else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(__carven_match_0)>, double>) {
            return "float";
        }
         else {
            return "other";
        }
    }());
}

auto main() noexcept -> int {
    const auto result = ([&]() noexcept {
        auto&& __carven_match_1 = 2;
        if (__carven_match_1 == 1) {
            return "one";
        }
         else if (__carven_match_1 == 2) {
            return "two";
        }
         else {
            return "other";
        }
    }());
    std::println("{} {}", result, classify(42));
}

