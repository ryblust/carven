# Carven language server

`carven-lsp` is a Carven application using `std::json`, `std::process`, `std::io`,
and `std::channel`. It owns LSP JSON, editor versions, scheduling, and UTF-16
positions. The sibling `carven-analyzer` process serves compiler and Workspace
queries over a private pipe.

## Run

Install the toolchain and run `carven-lsp` over stdio. It uses `carven` and
`carven-analyzer` beside its executable, with `.exe` names on Windows.
`--analyzer <program> [args...]` selects a transport fixture for process tests
and debugging.

The separate `carven-lang.carven` VS Code extension starts `carven-lsp` from
PATH. Set `carven.serverPath` to select a built server in an existing window;
saving the setting restarts it. The extension contains its language client and
metadata; VS Code supplies the JavaScript runtime.

## Projects

Each workspace folder uses its nearest ancestor containing `build.cv` or
`xmake.lua`, preferring `build.cv` when both exist. The project script owns target
membership:

- Native projects run the sibling compiler's `carven query`. Scripts publish a
  plan through `std::build.project(plan)`. Query executes the real script,
  including code around that call, and returns targets and script dependencies.
- Xmake projects query the configured targets and Carven rule's source inputs.
  Configure the project and install its dependencies first. The LSP refreshes
  configuration with `xmake f -y --require=no`, then reads the target model.
  `initializationOptions.xmakePath` selects the Xmake executable.

Project queries execute trusted project code. The VS Code extension starts the
server only in trusted workspaces. Application syntax/type errors permit target
discovery; project-script errors can prevent it.

Default targets and targets owning open documents are analyzed.
`initializationOptions.target` restricts the selection. Unopened sources
participate in diagnostics and cross-file queries. Untitled buffers and files
outside selected targets use standalone inputs with the same standard library.

Editor text overlays disk content until `didClose`. Incremental edits use UTF-16
ranges; stale versions are ignored. Dynamic watchers cover project roots, scripts,
and source directories. Clients without dynamic watching supply file events.
`carven.reload` refreshes the project. Clients opting into `carven/projectStatus`
receive loading, ready, and error states.

## Queries

| Area | Behavior |
| --- | --- |
| Diagnostics | Errors, warnings, notes, help, and related locations, including unopened sources |
| Hover and signature help | Source signatures, named parameters, and known types |
| Definition, references, highlights | Semantic identities and local import aliases |
| Completion | Scoped names, members, import paths/selections, and auto-import edits |
| Rename | Identifier/binding checks and versioned edits; aliases retain local identity |
| Symbols and call hierarchy | Retained declarations, proven callable targets, and source call sites |
| Semantic tokens and inlay hints | Identifier roles, inferred types, and source argument names |
| Folding | Code, import, and comment ranges from retained syntax |
| Formatting and code actions | Canonical formatting, diagnostic fixes, and import organization |

Completion replaces the complete identifier span. `.` and `:` trigger member
queries. The adapter combines completion/import edits that insert at the same
position. Visibility and type filtering use compiler facts. Native imports expose
known C++ paths and declared Carven signatures; C++ header contents remain opaque.

Semantic tokens support full and range requests. Inlay hints carry their labels
without a resolve step. Supporting clients receive semantic-token and inlay-hint
refresh requests after edits. Document highlights omit read/write kinds because
source observations do not classify those accesses.

Formatting uses the fixed formatter style and requires complete syntax; semantic
errors are allowed. The returned edit preserves the source's control-flow syntax.
Organize Imports preserves aliases, comments, and authored groups. Unused selections
are removed only after successful checking. Clients apply returned edits through
their normal workflows. Standard-library overlays support queries and diagnostics,
while generated refactoring and formatting edits remain limited to project files.

Incomplete source retains original diagnostics and independently known facts.
Recovery remains partial: unresolved receiver types lack members, and damaged
module scopes can prevent binding lookup. Failed source bodies remain invalid.
Obsolete version-sensitive requests return `ContentModified`.
Document symbols and folding remain valid across project changes when their
open document's contents are unchanged.

## Transport and scheduling

Stdio uses bounded Content-Length frames. One asynchronous writer emits complete
frames serially. Byte channels connect input, analyzer work, and output; one analyzer
transaction runs at a time. Consecutive document notifications can merge, while
queries and close/project transitions preserve ordering boundaries.

Cancellation replies with `RequestCancelled` without interrupting analyzer CPU
work. Later edits preserve earlier request ordering. Only diagnostics for the
current project generation are published. Shutdown settles pending requests and
drains active analysis before the analyzer handshake. A normal `shutdown`/`exit`
sequence succeeds; premature exit or EOF fails.

## Validation

Build the compiler with `./xmakew build`, then `./xmakew build -g toolchain`.
Native framing contracts run through `./xmakew test`. The separate editor checks
live in `editors/vscode/tests/`: `lsp/` covers the protocol and project model,
and `integration.ts` covers VS Code. Setup is in `editors/vscode/development.md`.
