# Native project queries and generated inputs

- **Status:** Draft
- **Scope:** Querying native build declarations and publishing generated application inputs

## Summary

Let editor tooling obtain the same target names and source selections used by
`carven build`, before application compilation succeeds. Select a declaration
query contract without duplicating `build.cv` or introducing another project
configuration. Tracked generated-input publication remains deferred.

## Context

`carven build` interprets `build.cv` as an ordinary program. Scripts construct
`BuildPlan` values and call `std::build.build` to build targets and apply CLI
run selection. The Standard Craft owns
source selection, object reuse, subprocess scheduling, and native outputs. The
compiler supplies compilation results and matching resource inputs.

The current project root is the working directory. Compiler inputs form an
explicit owned source set; module identities derive from root-relative paths.
Installed toolchain sources supply dependencies. The compiler's artifact manifest
describes generated outputs after analysis; it is not a project declaration.

Scripts can execute arbitrary actions before and between build calls.
They have no declaration-only query contract. An LSP needs target and source information
while application source is incomplete and no successful build exists.

## Design

### Ownership and query scope

Standard Craft owns target declarations, defaults, source selection, and build
policy. Builtin host operations supply bounded file, process, or compilation
contracts where selected. Compiler knowledge checks those calls and interprets
the declaration code; it also supplies source/module identities and compilation
results. Runtime Craft supplies shared value and operation support, while native
file/process adapters remain with their Crafts and interpreted host providers.

The CLI exposes declaration-query results to Workspace and editor consumers.
Select declaration/action separation and refresh inputs for the first query
delivery. Arbitrary-script purity analysis, tracked generation, and a general
task graph have separate consumers and are not prerequisites for that delivery.

### Shared declarations

Use the same `BuildPlan` and `BuildTarget` data for querying and building. Target naming and membership have one source of truth. Compilation
and linking consume selected declarations after declaration evaluation completes.
The API for exposing plan data to a query remains open.

The query returns the project root, target names, exact application source sets,
and matching toolchain/Crafts inputs needed by semantic analysis. Module
identities use the existing path rule. Include the declaration inputs needed to
refresh the selection after edits. Build cache and generated-object details are
outside this editor contract.

The query does not generate or compile application C++, link executables, or
require an artifact manifest. Invalid application source does not prevent target
discovery. An invalid build declaration returns a query failure rather than an
empty successful project. A consumer may retain an earlier result only with its
original revision and failure state visible.

### Evaluation boundary

`build.cv` is trusted Carven code. Declaration selection may read files or
environment values. A query is therefore not a promise of arbitrary script
purity or sandboxing. Define which capabilities declaration evaluation needs,
and separate them from target build actions; do not implement querying by
silently suppressing process calls in an otherwise executing build.

## Open decisions

### OPEN-01 — Separate declarations from execution

Select a query API that lets ordinary scripts expose the same plan data used for
building. Keep program entry semantics independent of project queries. Close the
decision using single-target and shared-library projects, with the query and
build operations consuming the same source selections.

### OPEN-02 — Discovery and refresh inputs

Define project-root selection for an editor opened at the root or a subdirectory,
and the declaration files, source inventories, and toolchain inputs that trigger
a refresh. Distinguish source-content changes from changes to target membership.
State how conditional file/environment reads affect selection freshness.

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

Use one single-source project and one `build.cv` with two executables sharing a library.
Compare queried and built source selections. Query before any build and while
application source contains errors; verify no application compilation or linking
occurs. Reject malformed build declarations explicitly. Change target membership,
declaration dependencies, and toolchain inputs and verify selection refresh.
