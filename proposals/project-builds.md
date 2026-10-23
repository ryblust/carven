# Project declaration freshness and generated inputs

- **Status:** Draft
- **Scope:** Declaration file/environment dependencies, refresh, and generated application inputs

## Summary

Define declaration freshness when build scripts read additional files or
environment values beyond their loaded source inputs and watched project roots.
Tracked generated-input publication remains deferred.

## Context

`project(plan)` resolves one `BuildPlan` for query, build, run, or test. Querying
reports target declarations without compiling application sources. Its JSON
contains `project` with root, targets, and source roots, plus `scripts` listing
loaded source files, including indirect build-script imports. Retained script
analysis validates its source inputs; each query executes the script again to
observe external files and environment values.

The language server discovers `build.cv` or `xmake.lua`, watches project roots and
reported scripts/source roots, and refreshes membership after relevant edits or
`carven.reload`. Failed discovery retains watch dependencies and an explicit error
state while discarding that project's target inputs.

Queries do not trace arbitrary file or environment observations. A fresh query
observes those values, but a change outside watched inputs need not trigger one.
Scripts can execute arbitrary actions around `project`; only its query branch
avoids application compilation and linking. Queryable scripts publish one project
JSON result on stdout.

## Design

### Ownership and query scope

Standard Craft owns target declarations, defaults, source selection, and build
policy. Builtin host operations supply bounded file, process, or compilation
contracts where selected. Compiler knowledge checks those calls and interprets
the declaration code; it also supplies source/module identities and compilation
results. Runtime Craft supplies shared value and operation support, while native
file/process adapters remain with their Crafts and interpreted host providers.

The CLI exposes declaration-query results to Workspace and editor consumers.
Extend the existing refresh inputs only when their observations can be owned and
invalidated consistently. Arbitrary-script purity analysis, tracked generation,
and a general task graph have separate consumers.

### Declaration freshness

Preserve the shared resolved plan and root-relative source identities. Identify
which additional declaration inputs change target membership, including file
contents, directory inventories, toolchain inputs, and environment values.
Separate those dependencies from ordinary application source-content updates.
Build cache and generated-object details remain outside the editor contract.

An invalid build declaration remains a query failure rather than an empty
successful project. If a consumer retains an earlier selection, retain its
original revision and expose the failed refresh state.

### Evaluation boundary

`build.cv` is trusted Carven code. Declaration selection may read files or
environment values. A query is therefore not a promise of arbitrary script
purity or sandboxing. Define how additional observations participate in freshness
without silently suppressing ordinary script operations.

## Open decisions

### OPEN-02 — Discovery and refresh inputs

The current ancestor discovery and root/script watchers establish ordinary
project refresh. Select a contract for conditional file/environment reads and
declaration dependencies outside those roots. Determine how toolchain changes
invalidate selections. Close this decision by comparing refreshed selections
with a fresh query for each supported dependency, including failed reads.

## Deferred work

### DEFER-03 — Tracked generated-input publication

- **Reason deferred:** Scripts can already generate files and run tools, but no
  selected workflow requires reusable, transactionally published generated inputs.
- **Reactivation condition:** A project generates application source or resources
  and needs tracked reruns or protection from partial generation.

Analyze the build entry before it produces application inputs; reject a cycle
requiring its own outputs to analyze that entry. Define output ownership,
obsolete-output removal, and successful publication so failure or interruption
does not expose partial output as a completed input. Distinguish the executing
host from the generated program's target.

Select a declaration or observation model for file, environment, and tool
dependencies. Define reruns for untracked observations and effects whose
execution count matters. Validate unchanged runs and changed inputs against a
fresh build before enabling reuse. A task graph requires a workflow that needs
it; source generation alone does not select that model.

## Validation

Compare refreshed selections with fresh queries after loaded-script edits,
declaration inputs outside the project, environment changes, failed reads, and
toolchain replacement. Verify source membership and failure states for each
supported dependency. Query before any build and while application sources have
errors; declaration refresh must not compile or link application outputs.
Generated-input experiments additionally cover unchanged runs, input changes,
failed generation, obsolete outputs, and interruption before publication.
