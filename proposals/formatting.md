# Formatting and output composition

- **Status:** Exploration
- **Implementation:** Not started for the extensions below
- **Scope:** Broader destination planning and composition across formatting/output boundaries
- **Depends on:** A concrete workload and the existing text/output contracts

## Current foundation

Complete and partial builtin interpolation precomputation, parsed integer
formatting, proved UTF-8 adoption, explicit formatted append, and known scalar
output are implemented. The remaining work concerns broader reservation
policies, composition across observation boundaries, and runtime compatibility.

## Open decisions

### OPEN-01 — Broader destination capacity planning

- **Status:** Blocked on a representative workload beyond the implemented integer path
- **Closure condition:** Specify length arithmetic, reservation bounds, formatter
  invocation count, and resource observations; measure the complete operation.

An exact length, an upper bound, and a minimum field width are distinct facts.
An upper-bound reservation can request more memory than the actual output needs;
computing the actual size can add work. Reused and fresh storage can favor
different choices. No general size solver or new reservation policy is selected.
Explicit operation resource constraints take precedence over growth heuristics.

### OPEN-02 — Formatting used by printing

- **Status:** Blocked on a concrete consumer and an observable-behavior contract
- **Closure condition:** Define legal composition that preserves operand completion,
  String observation, failure prefixes, and resource behavior before measuring it.

```carven
println(f"{text}", mutate(&text));
```

The owning interpolation completes before the later argument mutates `text`.
Within one interpolation, String holes instead observe their owners after all
holes evaluate. Moving either observation boundary changes behavior.

Streaming segments may expose a prefix before a failure that formerly occurred
while constructing the complete String. Batching print values may suppress a
prefix that the current per-value output already produced. A legal design must
state which construction and conversion resource observations it preserves;
allocation counts alone do not define that contract. No output fusion is selected.

## Deferred work

### DEFER-03 — Replace floating conversion or the complete formatting runtime

- **Reason deferred:** No consumer requires a replacement with defined compatibility.
- **Reactivation condition:** A concrete compatibility contract and representative
  cost evidence justify evaluating a replacement.

No replacement is selected. Host formatting does not establish that a
consumer's standard library produces identical bytes. Unicode width/precision,
locale, floating presentation, custom formatters, and invalid dynamic widths
need explicit treatment. Unsupported valid specifications retain the runtime
path; folding must preserve validation and diagnostics.
In particular, moving an invalid dynamic-width failure from execution to a
compile-time diagnostic requires an explicit language decision. Extensions must
also preserve the supported host/target data-model requirement and the current
boundary for floating-point constant facts.

## Validation

A selected extension must cover effects, snapshots and aliases, temporary backing,
checked fallback, formatting/allocation failure and visible output prefixes.
Measure generated source, native compilation, runtime, and memory separately with
representative inputs. A native kernel measurement does not validate compiler
selection or establish an application throughput gain.
