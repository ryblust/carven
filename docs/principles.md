# Design principles

Carven lets programmers express intent and supplies the routine boilerplate
and mechanism composition a skilled C++ programmer would otherwise write.
These principles provide criteria for evaluating design tradeoffs and admitting
features.

## Source intent

Source should expose distinctions that affect how a programmer reasons about
access, ownership, mutation, lifetime, control, failure, allocation, or
interoperability. A source distinction needs an observable operation,
guarantee, cost, or boundary; equivalent C++ mechanisms do not require separate
source features.

Representation choices remain with the implementation when they preserve the
promised behavior. Parsing should depend on tokens and delimiters rather than
semantic lookup, so a source form keeps the same structure across scopes.

An omitted spelling needs a bounded, documented rule. Type context may remove
repeated type information, but must not silently introduce ownership transfer,
mutable access, capture, failure handling, or a lifetime extension. An inference
rule states its inputs, priority, stopping conditions, and incompatible cases.

## Semantic authority

Carven determines the meaning and validity of its own operations. C++ realizes
that meaning; a representation choice must not silently add a language rule.
Each requirement must follow from a Carven rule or an explicit boundary
contract, rather than emerge from the chosen implementation.

A boundary may delegate native capabilities to C++ without reproducing its type
system or library internals in Carven analysis. Its contract must identify the
required operations and the guarantees delegated to C++. C++ traits and
protocols may check those requirements, but declaring an implementation
contract does not justify imposing incidental requirements on admitted source
operations.

Each fact has one authority. Later consumers should use established facts
rather than reconstruct meaning from generated names or text. Reusable
libraries and native build tools retain their own responsibilities; integration
with Carven does not grant them authority over its language rules.

## Requirements follow operations

An implementation should require only the capabilities needed by the promised
operation. Reading an object does not require copying it; transferring an owner
does not require default construction or assignment. A temporary, wrapper, or
storage strategy must accommodate admitted types rather than narrow them for
implementation convenience.

This applies equally to compiler-generated types and library-backed types.
Native construction and library protocols may implement an operation without
requiring Carven to duplicate their internal logic.

## Guarantees match enforcement

Safety claims must match enforced checks and stated assumptions. A restriction
should identify the guarantee it provides; an escape boundary should identify
the guarantees entrusted to its author. Diagnostics should distinguish a
Carven rule violation from a native contract violation at that boundary.

Checking access, ownership, and lifetime does not establish the functional
correctness of a type's copy, move, assignment, or business logic. Designs must
make that distinction clear when stating what Carven guarantees.

## Cost follows behavior

The cost baseline is skilled handwritten C++ preserving the same guarantees,
including evaluation order, ownership, and safety checks. Prefer direct native
operations when they express those guarantees. Correctness must not depend on
an optional optimizer transformation, and performance claims require
measurement of the relevant workload.

Compile-time distinctions should leave no runtime state unless execution still
needs them. A runtime object, allocation, indirection, or synthetic control
mechanism needs an identifiable behavioral requirement. Persistent storage or
compiler representations need a later consumer and a reason the information
must survive until then; an analysis representation does not by itself justify
a corresponding runtime representation.

## Feature admission

A feature needs a user intent or guarantee that existing forms do not adequately
express. Technical feasibility alone is insufficient: its benefit must justify
its semantic, runtime, interoperability, and tooling costs.

A supported slice may be small, but every accepted form must have a coherent
meaning that can be implemented and verified across its boundaries. Unsupported
neighboring forms must be rejected. Syntax should follow settled meaning rather
than reserve forms for an undecided design.
