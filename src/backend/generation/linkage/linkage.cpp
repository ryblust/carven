module carven:backend.generation.linkage.impl;

import :backend.generation.linkage;
import std;

namespace {

constexpr auto linkage_domain_tag = std::string_view("carven-linkage-domain-v2");
constexpr auto explicit_domain_tag = std::string_view("explicit");
constexpr auto artifact_root_domain_tag = std::string_view("artifact-root");
constexpr auto no_tests_tag = std::string_view("tests:none");
constexpr auto external_tests_tag = std::string_view("tests:external-runner");
constexpr auto default_tests_tag = std::string_view("tests:default-runner");
constexpr auto module_namespace_tag = std::string_view("carven-module-namespace-v1");

class Sha256 final {
public:
    auto append(std::span<const std::uint8_t> bytes) noexcept -> void {
        total_bytes += bytes.size();
        for (const auto byte : bytes) {
            pending[pending_size++] = byte;
            if (pending_size == pending.size()) {
                compress(pending);
                pending_size = 0;
            }
        }
    }

    auto append(std::string_view text) noexcept -> void {
        append(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    }

    auto append_u64(std::uint64_t value) noexcept -> void {
        auto bytes = std::array<std::uint8_t, 8> {};
        for (auto index = 0uz; index < bytes.size(); ++index) {
            bytes[bytes.size() - index - 1] = static_cast<std::uint8_t>(value >> (index * 8));
        }
        append(bytes);
    }

    auto append_field(std::string_view value) noexcept -> void {
        append_u64(value.size());
        append(value);
    }

    auto finish() noexcept -> std::array<std::uint8_t, 32> {
        const auto bit_count = total_bytes * 8;
        append(std::array<std::uint8_t, 1> {0x80});
        while (pending_size != 56) {
            append(std::array<std::uint8_t, 1> {0});
        }
        append_u64(bit_count);

        auto digest = std::array<std::uint8_t, 32> {};
        for (auto word_index = 0uz; word_index < state.size(); ++word_index) {
            const auto word = state[word_index];
            for (auto byte_index = 0uz; byte_index < 4; ++byte_index) {
                digest[word_index * 4 + byte_index] =
                    static_cast<std::uint8_t>(word >> ((3 - byte_index) * 8));
            }
        }
        return digest;
    }

private:
    static constexpr auto round_constants = std::array<std::uint32_t, 64> {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
        0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
        0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
        0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
        0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
        0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
        0xc67178f2u,
    };

    static constexpr auto choose(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept
        -> std::uint32_t {
        return (x & y) ^ (~x & z);
    }

    static constexpr auto majority(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept
        -> std::uint32_t {
        return (x & y) ^ (x & z) ^ (y & z);
    }

    static constexpr auto large_sigma_0(std::uint32_t value) noexcept -> std::uint32_t {
        return std::rotr(value, 2) ^ std::rotr(value, 13) ^ std::rotr(value, 22);
    }

    static constexpr auto large_sigma_1(std::uint32_t value) noexcept -> std::uint32_t {
        return std::rotr(value, 6) ^ std::rotr(value, 11) ^ std::rotr(value, 25);
    }

    static constexpr auto small_sigma_0(std::uint32_t value) noexcept -> std::uint32_t {
        return std::rotr(value, 7) ^ std::rotr(value, 18) ^ (value >> 3);
    }

    static constexpr auto small_sigma_1(std::uint32_t value) noexcept -> std::uint32_t {
        return std::rotr(value, 17) ^ std::rotr(value, 19) ^ (value >> 10);
    }

    auto compress(std::span<const std::uint8_t, 64> block) noexcept -> void {
        auto schedule = std::array<std::uint32_t, 64> {};
        for (auto index = 0uz; index < 16; ++index) {
            schedule[index] = (static_cast<std::uint32_t>(block[index * 4]) << 24)
                | (static_cast<std::uint32_t>(block[index * 4 + 1]) << 16)
                | (static_cast<std::uint32_t>(block[index * 4 + 2]) << 8)
                | static_cast<std::uint32_t>(block[index * 4 + 3]);
        }
        for (auto index = 16uz; index < schedule.size(); ++index) {
            schedule[index] = small_sigma_1(schedule[index - 2]) + schedule[index - 7]
                + small_sigma_0(schedule[index - 15]) + schedule[index - 16];
        }

        auto [a, b, c, d, e, f, g, h] = state;
        for (auto index = 0uz; index < schedule.size(); ++index) {
            const auto temporary_1 =
                h + large_sigma_1(e) + choose(e, f, g) + round_constants[index] + schedule[index];
            const auto temporary_2 = large_sigma_0(a) + majority(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + temporary_1;
            d = c;
            c = b;
            b = a;
            a = temporary_1 + temporary_2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    std::array<std::uint32_t, 8> state {
        0x6a09e667u,
        0xbb67ae85u,
        0x3c6ef372u,
        0xa54ff53au,
        0x510e527fu,
        0x9b05688cu,
        0x1f83d9abu,
        0x5be0cd19u,
    };
    std::array<std::uint8_t, 64> pending {};
    std::size_t pending_size = 0;
    std::uint64_t total_bytes = 0;
};

auto identity128(Sha256 digest) noexcept -> std::array<std::uint8_t, 16> {
    const auto full = digest.finish();
    auto truncated = std::array<std::uint8_t, 16> {};
    std::ranges::copy(full | std::views::take(truncated.size()), truncated.begin());
    return truncated;
}

auto hex128(std::span<const std::uint8_t, 16> value) noexcept -> std::string {
    constexpr auto digits = std::string_view("0123456789abcdef");
    auto result = std::string();
    result.reserve(value.size() * 2);
    for (const auto byte : value) {
        result += digits[byte >> 4];
        result += digits[byte & 0x0f];
    }
    return result;
}

} // namespace

LinkageDomainID::LinkageDomainID(std::array<std::uint8_t, 16> bytes) noexcept
    : value(bytes) {}

auto LinkageDomainID::hex() const noexcept -> std::string {
    return hex128(value);
}

auto LinkageDomainID::namespace_identifier() const noexcept -> std::string {
    return std::format("d_{}", hex());
}

auto derive_linkage_domain_id(const TargetGenerationRequest& request) noexcept -> LinkageDomainID {
    auto digest = Sha256();
    digest.append_field(linkage_domain_tag);
    switch (request.tests) {
        case TestEmissionMode::None:           digest.append_field(no_tests_tag); break;
        case TestEmissionMode::ExternalRunner: digest.append_field(external_tests_tag); break;
        case TestEmissionMode::DefaultRunner:  digest.append_field(default_tests_tag); break;
    }
    switch (request.linkage_domain.kind()) {
        case LinkageDomainKind::Explicit:     digest.append_field(explicit_domain_tag); break;
        case LinkageDomainKind::ArtifactRoot: digest.append_field(artifact_root_domain_tag); break;
    }
    digest.append_field(request.linkage_domain.value());
    return LinkageDomainID(identity128(std::move(digest)));
}

ModuleNamespaceID::ModuleNamespaceID(std::array<std::uint8_t, 16> bytes) noexcept
    : value(bytes) {}

auto ModuleNamespaceID::hex() const noexcept -> std::string {
    return hex128(value);
}

auto ModuleNamespaceID::namespace_identifier() const noexcept -> std::string {
    return std::format("m_{}", hex());
}

auto derive_module_namespace_id(std::string_view canonical_module_path) noexcept
    -> ModuleNamespaceID {
    auto digest = Sha256();
    digest.append_field(module_namespace_tag);
    digest.append_field(canonical_module_path);
    return ModuleNamespaceID(identity128(std::move(digest)));
}
