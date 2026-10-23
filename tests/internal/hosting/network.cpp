module;

#include <carven/hosting/api.hpp>
#include <carven/hosting/operation.hpp>
#include <carven/std/net/native.hpp>

#if !defined(_WIN32)
#include <netinet/in.h>
#endif

module carven:test.internal.hosting.network;

import :test.harness.framework;
import std;

namespace {

namespace net = carven::standard::net;

class Loopback final {
public:
    Loopback() noexcept;
    ~Loopback() noexcept;
    auto events() noexcept -> net::Network&;
    auto reader() const noexcept -> net::View;
    auto writer() const noexcept -> net::View;
    auto payload() const noexcept -> std::uint8_t;
    auto set_payload(std::uint8_t value) noexcept -> void;

private:
    net::Network network;
    std::optional<net::Connection> receiving;
    std::optional<net::Connection> sending;
    std::uint8_t value;
};

thread_local Loopback* active_network = nullptr;

Loopback::Loopback() noexcept
    : network(),
      receiving(),
      sending(),
      value(42) {
    require(network.startup_error() == 0);
    const auto listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    require(listener != net::invalid_socket);
    auto address = sockaddr_in {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    require(::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
    require(::listen(listener, 1) == 0);
#if defined(_WIN32)
    auto length = static_cast<int>(sizeof(address));
#else
    auto length = static_cast<socklen_t>(sizeof(address));
#endif
    require(::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) == 0);
    const auto writer = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    require(writer != net::invalid_socket);
    require(::connect(writer, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
    const auto reader = ::accept(listener, nullptr, nullptr);
    require(reader != net::invalid_socket);
    net::close_socket(listener);

    receiving.emplace(network, reader);
    sending.emplace(network, writer);
    require(receiving->startup_error() == 0 && sending->startup_error() == 0);
    active_network = this;
}

Loopback::~Loopback() noexcept {
    active_network = nullptr;
}

auto Loopback::events() noexcept -> net::Network& {
    return network;
}

auto Loopback::reader() const noexcept -> net::View {
    return {.connection = &*receiving};
}

auto Loopback::writer() const noexcept -> net::View {
    return {.connection = &*sending};
}

auto Loopback::payload() const noexcept -> std::uint8_t {
    return value;
}

auto Loopback::set_payload(std::uint8_t next) noexcept -> void {
    value = next;
}

auto reader() noexcept -> net::View {
    return active_network->reader();
}

auto writer() noexcept -> net::View {
    return active_network->writer();
}

auto payload() noexcept -> std::uint8_t {
    return active_network->payload();
}

auto standard_source(void*, std::string_view module_path) noexcept
    -> std::expected<std::optional<HostSource>, HostError> {
    constexpr auto prefix = std::string_view("crafts.carven.std.");
    if (!module_path.starts_with(prefix)) {
        return std::nullopt;
    }
    auto relative = std::string(module_path.substr(prefix.size()));
    std::ranges::replace(relative, '.', '/');
    const auto filename = std::format("crafts/carven/std/{}.cv", relative);
    auto file = std::ifstream(filename, std::ios::binary);
    if (!file) {
        return std::unexpected(hosting_error(HostErrorKind::Input, "cannot read " + filename));
    }
    return HostSource {
        .module_path = std::string(module_path),
        .origin = filename,
        .text = std::string(std::istreambuf_iterator<char>(file), {})
    };
}

auto expect_value(HostSession& session, std::int32_t expected) noexcept -> void {
    auto task = session.submit("value", {});
    if (!expect(task).note([&]() noexcept { return task.error().message; })) {
        return;
    }
    for (auto slice = 0; slice < 32 && task->result() == nullptr; ++slice) {
        session.poll(64);
    }
    if (!expect(task->result() != nullptr)) {
        return;
    }
    const auto& result = *task->result();
    if (!expect(result).note([&]() noexcept { return result.error().message; })) {
        return;
    }
    const auto* value = std::get_if<HostValue>(&*result);
    const auto* integer = value ? std::get_if<std::int32_t>(value) : nullptr;
    if (expect(integer != nullptr)) {
        expect_equal(*integer, expected);
    }
}

const TestSuite suite([] static noexcept {
    "Hosting network: concurrent static I/O shares its source and runs again on reload"_test =
        [] static noexcept {
            auto network = Loopback();
            const auto services = std::array {
                host_cpp_operation<reader>("probe::reader")
                    .with_static_execution(HostServiceEffects::Environment),
                host_cpp_operation<writer>("probe::writer")
                    .with_static_execution(HostServiceEffects::Environment),
                host_cpp_operation<payload>("probe::payload")
                    .with_static_execution(HostServiceEffects::Environment),
                host_cpp_operation<net::read>("carven::standard::net::read", {}, &network.events())
                    .with_static_execution(HostServiceEffects::Environment),
                host_cpp_operation<net::write_all>(
                    "carven::standard::net::write_all",
                    {},
                    &network.events()
                )
                    .with_static_execution(HostServiceEffects::Environment),
                host_cpp_operation<&net::ReadResult::error>("error").with_static_execution(
                    HostServiceEffects::None
                ),
                host_cpp_operation<&net::ReadResult::code>("code").with_static_execution(
                    HostServiceEffects::None
                ),
                host_cpp_operation<&net::ReadResult::end>("end").with_static_execution(
                    HostServiceEffects::None
                ),
                host_cpp_operation<&net::ReadResult::take_bytes>("take_bytes")
                    .with_static_execution(HostServiceEffects::None),
                host_cpp_operation<&net::WriteResult::error>("error").with_static_execution(
                    HostServiceEffects::None
                ),
                host_cpp_operation<&net::WriteResult::code>("code").with_static_execution(
                    HostServiceEffects::None
                ),
                host_cpp_conversion<net::View>("carven::standard::net::View")
                    .with_static_execution(HostServiceEffects::None),
                host_cpp_conversion<std::uint8_t>().with_static_execution(HostServiceEffects::None),
                host_cpp_conversion<std::int32_t>().with_static_execution(HostServiceEffects::None),
                host_cpp_conversion<bool>().with_static_execution(HostServiceEffects::None),
                host_cpp_conversion<carven::runtime::Vector<std::uint8_t>>().with_static_execution(
                    HostServiceEffects::None
                ),
            };
            auto options = host_session_options();
            options.calls.allow_host_effects = true;
            options.services = services;
            constexpr auto source = std::string_view(R"(
import std::async using yield_once;
import std::net using { borrow, read, write_all, NetError }
import <carven/std/net/native.hpp> using carven::standard::net;
import "network-probe.hpp" using probe;

const async fn exchange() -> i32 throw NetError {
    let reader = borrow(probe::reader() as net::View);
    let writer = borrow(probe::writer() as net::View);
    let bytes: Vector<u8> = [probe::payload() as u8];
    async let receiving = read(reader, 1);
    await yield_once();
    await write_all(writer, &&bytes)?;
    return match await receiving? {
        .Data(value) => {
            assert(value.length() == 1);
            value[0] as i32
        },
        .End => -1,
    };
}

const observed = await exchange()?;
fn value() -> i32 => observed;
)");
            const auto sources = std::array {HostSource {
                .module_path = "crafts.carven.tests.hosted_net",
                .origin = "hosted-net.cv",
                .text = std::string(source)
            }};
            auto session = HostSession::load(
                "crafts.carven.tests.hosted_net",
                sources,
                options,
                {.context = nullptr, .resolve = standard_source}
            );
            if (!expect(session).note([&]() noexcept { return session.error().message; })) {
                return;
            }
            expect_value(*session, 42);

            network.set_payload(43);
            const auto reloaded =
                session->reload(sources, {.context = nullptr, .resolve = standard_source});
            if (expect(reloaded).note([&]() noexcept { return reloaded.error().message; })) {
                expect_value(*session, 43);
            }
        };
});

} // namespace
