#include "providers/generated_native.hpp"

#include <carven/api/tests/interop/providers/generated.hpp>
#include <carven/api/tests/interop/providers/generated/Record.hpp>
#include <carven/providers/tests/interop/providers/contracts.hpp>
#include <carven/providers/tests/interop/providers/generated/generated_record/Result.hpp>

#include <type_traits>

namespace contract = generated_types;

static_assert(std::is_same_v<contract::generated_void::Result, carven::runtime::Void>);
static_assert(std::is_same_v<contract::generated_void::Completion, carven::runtime::Outcome<void>>);

static_assert(
    carven::providers::tests::interop::providers::contracts::cv_escaped_functions::contract_access::
        service.parameter_accesses[0]
    == carven::runtime::hosting::ParameterAccess::Take
);

static_assert(
    carven_type_name(static_cast<contract::generated_record::Result*>(nullptr))
    == "tests.interop.providers.generated.Record"
);

static_assert(std::is_same_v<
              carven::providers::tests::interop::providers::generated::generated_record::Result::
                  cv_escaped_functions::generated_namespace_child::Result,
              std::int32_t>);

auto generated_other_translation_unit() -> bool {
    namespace template_types = carven::providers::tests::interop::providers::contracts::
        cv_escaped_functions::contract_record;

    const auto copied = contract_record(
        template_types::Result {
            .text = carven::runtime::String::from_str("template"),
            .count = 42,
        }
    );
    auto result =
        generated_record(0, {.code = 17}, contract::generated_record::Parameter2::System(23));
    using Link = std::remove_const_t<std::remove_pointer_t<contract::generated_links::Parameter0>>;
    using Tail = std::remove_const_t<std::remove_pointer_t<decltype(Link::next)>>;
    auto link = Link {.value = 19, .next = nullptr};
    const auto tail = Tail {.value = 23, .previous = &link};
    link.next = &tail;
    return copied.success_if() != nullptr
        && copied.success_if()->value.count == 42
        && result.success_if() != nullptr
        && result.success_if()->value.count == 42
        && generated_links(&link) == 42
        && carven::api::tests::interop::providers::generated::Record::namespace_value() == 9;
}
