# Spatial Code Philosophy

## Overview

This document defines a unified specification for utilizing whitespace and vertical spacing in source code. Spacing is not a decorative afterthought or a tool for purely visual aesthetics; it is a critical semantic mechanism that communicates intent, structures architecture, and directly governs cognitive scannability. 

The core goal is to produce code that is logically clear, visually scannable, structurally elegant, and optimally balanced between density and readability.

## 1. Core Philosophy of Visual Rhythm

### 1.1 Whitespace as Semantic Signalling
Blank lines are explicit architectural signals used to denote logical boundaries. They must never be inserted arbitrarily or strictly for visual symmetry. 
* **Structure over Decoration:** Avoid adding decorative blank lines simply because a file "looks too dense" or a line feels isolated. Every empty line must actively declare a change in state, intent, or abstraction level.
* **Self-Documenting Spacing:** Prioritize structural layout, expressive naming, and explicit ordering over inline documentation. A well-spaced block of code reveals its internal architecture natively, drastically reducing the necessity for explanatory comments.

### 1.2 The "No-Comment" Scan Heuristic
To validate whether a function uses correct vertical formatting, perform the following mental check:
* Temporarily strip all comments from the function.
* Scan the remaining code vertically at a rapid pace.
* If the underlying architecture, execution flow, and structural phases remain immediately obvious through blocks alone, the spacing is correct.

## 2. Intra-Function Structural Grouping

### 2.1 Tight Statement Grouping
Statements that contribute directly to the same cohesive micro-operation must be grouped together tightly without any vertical separation. Introducing blank lines inside a hyper-focused block breaks context and introduces artificial friction.

**GOOD:**
```production
user = query_user_by_id(context.user_id)
profile = fetch_profile_record(user.id)
settings = extract_user_preferences(user.id)
```

**BAD:**
```production
user = query_user_by_id(context.user_id)

profile = fetch_profile_record(user.id)

settings = extract_user_preferences(user.id)
```

### 2.2 Phase-Based Transitions
Functions naturally progress through conceptual execution stages. Insert exactly **ONE** blank line when transitioning between different thinking phases to signal the shift in logic.

Typical execution phases include:
1. **Input / Loading:** Acquiring data and materials.
2. **Validation:** Evaluating integrity and enforcing invariants.
3. **Transformation:** Processing, converting, or calculating data.
4. **Side Effects:** Writing to databases, triggering events, or modifying state.
5. **Output / Return:** Packaging and delivering the final result.

**GOOD:**
```production
// Phase 1: Input
token = extract_auth_token(request)
user = resolve_user_from_token(token)

// Phase 2: Validation
if not user.is_active_admin:
    reject_unauthorized_access()

// Phase 3: Transformation
analytics_report = compile_system_report()

// Phase 5: Output
return format_render_output(analytics_report)
```

### 2.3 Block-Scanning vs. Vertical Fragmentation
Optimize your code layout for "block-scanning" (chunking) rather than line-by-line reading. Excessive single-line vertical spacing destroys reading momentum, degrades information density, and causes cognitive fragmentation.

**BAD (Vertical Fragmentation):**
```production
initialize_subsystem()

verify_hardware_flags()

establish_stream_connection()

start_worker_thread()
```

By removing the fragmented gaps, these atomic statements instantly coalesce into a single readable block representing the coherent conceptual phase of `[Subsystem Initialization]`.

## 3. Contextual & Abstraction Boundaries

### 3.1 Separating Abstraction Levels
When code shifts drastically from processing **low-level primitives/details** to evaluating **high-level domain/business logic**, you must introduce a vertical boundary. Separating these layers visually allows the brain to adjust its context-switching mechanism.

**GOOD:**
```production
// --- Low-level raw payload manipulation ---
raw_bytes = hardware_port.read_buffer()
checksum = calculate_crc32(raw_bytes)
if checksum != expected_crc32:
    return Error.CorruptedPayload

// --- High-level business logic validation ---
session_event = deserialize_packet(raw_bytes)

if session_event.action == ActionType.UserLogout:
    terminate_active_session(session_event.session_id)
    broadcast_audit_log("User logged out cleanly")
```

## 4. Consistency & Spacing Scale

### 4.1 Recommended Spacing Scale
Maintain a rigid, predictable rhythm of density across the entire codebase. Do not oscillate between loose and dense styles randomly within the same file.

* **0 Blank Lines:** Inside a tightly coupled, continuous logical micro-block.
* **1 Blank Line:** Between distinct logical sections, execution phases, or abstraction shifts within a function.
* **2 Blank Lines:** Optionally used between major macro-sections within a very large structural file, or between top-level declarations where language-specific style guides explicitly recommend it.

## 5. Final Philosophy Summary

| Concept | Spacing Treatment | Semantic Meaning |
|---|---|---|
| **Atomic Statements** | 0 blank lines (Tightly grouped) | Contributes to the exact same micro-operation. |
| **Phase Change** | 1 blank line | Transitioning (e.g., from *Validation* to *Transformation*). |
| **Abstraction Shift** | 1 blank line | Moving from low-level data work to high-level domain rules. |
| **Structural Blocks** | Clustered layout | Designed for rapid vertical chunk-scanning. |
| **Visual Elements** | Prohibited if purely aesthetic | Spacing must reveal architecture, not decorate syntax. |
