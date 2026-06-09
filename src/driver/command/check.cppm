export module carven.driver.command.check;

import carven.driver.request;
import std;

export auto execute([[maybe_unused]] const CheckRequest& request) noexcept -> int {
    std::println("carven check: not yet implemented");
    return 1;
}
