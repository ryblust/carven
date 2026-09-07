# Pointers through external API scenarios

- **Status:** Exploration
- **Implementation:** Not started
- **Scope:** Restricted addresses for C++ interoperability
- **Depends on:** None

## Summary

Start with external API requirements, then decide the language rules. The four
scenarios below pair small simulated C++ providers with proposed Carven clients.
The providers use C++20 and require no external SDK. Their interfaces deliberately
retain opaque types, native constness, and output parameters.

**The Carven snippets are requirements drafts, not runnable tests or supported
syntax.** In particular, pointer adoption from native results, parameter lowering,
and null narrowing are not implemented. Open questions are not compiler promises.
No snippet is registered in the build. The C++ snippets are assembled in order
for provider validation; the window declaration and implementation would be
separate files in an eventual interop fixture.

## Context

The intended primitive is a nullable, non-owning address value. Copying the address
does not copy its target or automatically manage its resources. Resource release
remains an API operation. Non-null does not imply that an external object is alive.

The current working vocabulary is `ptr<T>`, `nullptr`, `*p`, and `p->member`.
`->` applies only to Carven pointer values, without user-defined operator lookup.
Read/Write access is intended to govern target access: ordinary parameters and
`let` are read-only, Write parameters and `var` allow writes. The proposed native
parameter representations are `const T*` and `T*`, respectively, passed by value.
Taking a pointer with `&&` is not part of this proposal. These choices still need
to work through the scenarios below before the complete design is accepted.

## Scenarios

### 1. Opaque window: retain an address and hand it back

The application owns the obligation to destroy a created window. Carven does not
need its layout. A second address identifies the same window, not another window.

Provider header, proposed name `window.hpp`:

```cpp
#pragma once

namespace window_api {
struct Window;
auto create(bool succeed) noexcept -> Window*;
auto is_open(Window* window) noexcept -> bool;
auto close(Window* window) noexcept -> void;
auto destroy(Window* window) noexcept -> void;
}
```

Provider implementation (include `window.hpp` when placed in a separate file):

```cpp
#include <new>

namespace window_api {
struct Window final {
    bool open = true;
};

auto create(bool succeed) noexcept -> Window* {
    return succeed ? new (std::nothrow) Window {} : nullptr;
}

// This query deliberately accepts Window*, as many C handle APIs do.
auto is_open(Window* window) noexcept -> bool {
    return window->open;
}

auto close(Window* window) noexcept -> void {
    window->open = false;
}

auto destroy(Window* window) noexcept -> void {
    delete window;
}
}
```

Proposed Carven client:

```carven
import "window.hpp";

fn close_window(&window: ptr<::window_api::Window>) {
    if window != nullptr {
        ::window_api::close(&window);
    }
}

test "Pointers: opaque handles retain object identity" {
    var missing: ptr<::window_api::Window> = ::window_api::create(false);
    check(missing == nullptr);

    var window: ptr<::window_api::Window> = ::window_api::create(true);
    check(window != nullptr);
    if window != nullptr {
        var alias: ptr<::window_api::Window> = window;
        check(alias == window);
        check(::window_api::is_open(&window));
        close_window(&alias);
        check(!::window_api::is_open(&window));
        ::window_api::destroy(&window);
        // Both addresses are now invalid. Do not use either one.
    }
}
```

Required behavior:

- Store, copy, compare and pass an address without completing the opaque type.
- Copying does not create or destroy a window; closing through either address
  changes the one window. Explicit destruction occurs exactly once.
- A Write pointer parameter carries an address, not a reference to the caller's
  address slot. This scenario must not silently become `Window*&`.

Design pressure: `is_open` is observational but its native signature is `Window*`.
A Read Carven parameter mapped to `const Window*` cannot call it directly. The
draft uses `&window`; decide whether that native-signature-driven access marker
is acceptable. Do not infer native purity or insert a const-removing cast.

The proposed `check` does not itself establish non-nullness; the explicit branch
does. This test expects creation to succeed; an application would handle a null
result as creation failure.

### 2. Device: member access and two views of one object

This provider owns a device for the duration of the program. No client release
is needed. Proposed provider header `device.hpp`:

```cpp
#include <cstdint>

namespace device_api {
class Device final {
public:
    auto frames() const noexcept -> std::int32_t { return frames_; }

    auto draw() noexcept -> void { ++frames_; }

private:
    std::int32_t frames_ = 0;
};

inline auto device() noexcept -> Device* {
    static Device instance;
    return &instance;
}
}
```

Proposed Carven client:

```carven
import "device.hpp";

fn render(&device: ptr<::device_api::Device>) {
    if device != nullptr { device->draw(); }
}

fn frame_count(device: ptr<::device_api::Device>) -> i32 {
    if device == nullptr { return 0; }
    return device->frames() as i32;
}

test "Pointers: readonly access observes writes through another address" {
    var device: ptr<::device_api::Device> = ::device_api::device();
    check(device != nullptr);
    if device != nullptr {
        let before = frame_count(device);
        let observer: ptr<::device_api::Device> = device;
        render(&device);
        check(frame_count(observer) == before + 1);
    }
}
```

Required behavior: Read observes the shared target, rather than a snapshot of
its contents. Write permits its non-const native member. The helper checks its
own nullable parameter; it does not depend on interprocedural null inference.

Rejection requirements, to become separate diagnostic cases:

```carven
let observer: ptr<::device_api::Device> = ::device_api::device();
if observer != nullptr {
    observer->draw(); // Reject a non-const member through Read access.
}
var restored: ptr<::device_api::Device> = observer; // Reject permission recovery.
```

Also reject `device->draw()` when the address has not been checked. Reassignment
to a potentially null value invalidates the old check. Mutating the target
alone does not invalidate the address's non-null fact.

### 3. Catalog: retrieve a borrowed read-only object

The provider owns immutable metadata. A missing ID returns null; an existing
record remains valid for the program's duration. Proposed header `catalog.hpp`:

```cpp
#include <cstdint>

namespace catalog_api {
struct MeshInfo final {
    std::int32_t vertices;
};

inline auto find(std::int32_t id) noexcept -> const MeshInfo* {
    static const MeshInfo triangle {3};
    return id == 1 ? &triangle : nullptr;
}
}
```

Proposed direct use in Carven:

```carven
import "catalog.hpp";

test "Pointers: native readonly query results can be inspected" {
    let found: ptr<::catalog_api::MeshInfo> = ::catalog_api::find(1);
    check(found != nullptr);
    if found != nullptr { check(found->vertices == 3); }
    let missing: ptr<::catalog_api::MeshInfo> = ::catalog_api::find(99);
    check(missing == nullptr);
}
```

Reject native `const MeshInfo*` adoption into a writable binding and reject field
assignment through `found`. Do not copy the whole native object to work around
its constness.

The following is an **open requirement**, not an accepted or rejected test:

```carven
fn lookup(id: i32) -> ptr<::catalog_api::MeshInfo> {
    return ::catalog_api::find(id);
}
```

Can the application wrap its lookup while preserving the native read-only
result, or must this wrapper remain in C++? Evaluate that cost against this
actual query use case. The scenario does not prescribe new return syntax or
silently grant writable access at the call site.

### 4. Creation output: receive a new device address

The provider returns success separately from the address and sets the output to
null on failure. Success creates a resource requiring explicit destruction.
This is a simplified output protocol, not a COM implementation. Proposed header
`creation.hpp`, including the device declaration from scenario 2:

```cpp
#include <new>

namespace creation_api {
inline auto create(bool succeed, device_api::Device** result) noexcept -> bool {
    *result = nullptr;
    if (!succeed) {
        return false;
    }
    *result = new (std::nothrow) device_api::Device {};
    return *result != nullptr;
}

inline auto destroy(device_api::Device* device) noexcept -> void {
    delete device;
}

// Candidate thin adapter. This particular API has no error detail to preserve.
inline auto create_or_null(bool succeed) noexcept -> device_api::Device* {
    auto* result = static_cast<device_api::Device*>(nullptr);
    create(succeed, &result);
    return result;
}
}
```

Candidate A, using the adapter:

```carven
import "creation.hpp";

test "Pointers: creation adapter returns a resource address" {
    var device: ptr<::device_api::Device> = ::creation_api::create_or_null(true);
    check(device != nullptr);
    if device != nullptr {
        device->draw();
        check(device->frames() == 1);
        ::creation_api::destroy(&device);
    }
    var missing: ptr<::device_api::Device> = ::creation_api::create_or_null(false);
    check(missing == nullptr);
}
```

Candidate B, direct output, intentionally written as an operation sequence
instead of invented Carven syntax:

```text
Initialize a writable Device address slot to nullptr.
Call create(true, <explicit output access to that address slot>).
Inspect success and explicitly check the new address for null.
Draw through the new address; then destroy the created device.
Repeat with create(false, ...); expect failure and a null output.
```

Decide whether the thin adapter is sufficient for the first version. If direct
output is required, it needs an explicit way to expose address storage. Under
the proposed `&ptr<T> -> T*` mapping, ordinary `&device` cannot also mean `T**`.
Do not assume that `addressof(device)` is supported before deciding whether
addresses of pointer slots belong in the restricted model.

Real providers may return detailed status codes; their adapters must preserve
those results rather than reducing all failures to null.

## Decision record

- **PTR-1:** Drive the design with external signatures and observable client
  behavior. No native SDK download is required for these simulations.
- **PTR-2:** Keep requirements drafts outside runnable examples and language
  references until the compiler implements their accepted semantics.
- **PTR-3:** Do not remove native constness, automatically destroy resources,
  or promise external lifetime checking to make a scenario pass.

## Open decisions

1. **OPEN-ACCESS:** Does the Read/Write mapping remain usable for opaque C APIs
   whose observational operations take mutable pointer types? Scenario 1 is
   the first decision to review.
2. **OPEN-RETURN:** Is direct use of native read-only results sufficient, or must
   Carven helpers also return them? Scenario 3 makes the restriction concrete.
3. **OPEN-OUTPUT:** Is a thin creation adapter acceptable, or is direct address-slot
   output required? Scenario 4 separates that decision from target mutation.

Pointer storage in aggregates, taking the address of Carven-owned storage,
buffer views, and callback/context pairs require further scenarios if selected
for the first implementation. These four do not establish coverage of every
interop need or decide those features by omission.

## Validation and handoff

On 2026-09-08, all five C++ blocks were assembled into a temporary translation
unit and compiled with Clang in C++20 mode (`-Wall -Wextra -Werror`). The native
smoke client passed the checks listed below. No Carven snippet has been compiled.

Validate the provider snippets with a C++20 compiler and a native smoke client
covering successful and failed creation, shared identity, device mutation,
catalog hit/miss, output overwrite, and explicit cleanup. Native checks validate
only the simulated API, not the proposed Carven semantics.

After the decisions are settled, migrate the accepted clients to executable
interop tests, place rejection cases at their diagnostic boundary, and derive
an interop tutorial from the accepted scenarios. Tests should assert behavior,
not the compiler's representation or a history of implementation mistakes.
Update grammar, semantics and architecture documentation when implementation
makes those contracts real.
