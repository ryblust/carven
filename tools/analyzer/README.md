# Analyzer service

`carven-analyzer` is the persistent C++ process over compiler and Workspace modules.
It loads disk sources, applies editor overlays, and dispatches Workspace queries.
The Carven LSP owns project commands, scheduling, LSP JSON, and UTF-16 conversion.
The analyzer returns detached descriptions and UTF-8 byte spans.

Build with `./xmakew build`, then `./xmakew build carven-analyzer`. The toolchain
installs it beside `carven-lsp` and `carven`, with Crafts in the sibling `crafts/`
directory. Development builds use the enclosing checkout's Crafts through the
compiler's shared toolchain resolver.

## Project sources

For each workspace folder, discovery finds the nearest ancestor containing
`build.cv` or `xmake.lua`, preferring `build.cv` when both exist. The LSP executes
the project query and supplies roots, script dependencies, named targets, source
paths, and default selections. Application syntax/type errors permit discovery;
project-script errors can prevent it.

The analyzer uses the compiler's rooted source collector and module locator.
It follows imports through Workspace's retained syntax and loads missing helpers
on demand. Each target's explicit roots and import closure define its semantic
inputs. Other Craft candidates remain available for import queries.

One disk-source model owns standard and project documents. The document registry
resolves file URIs to physical paths and binds each open URI until close.
Filesystem links and Windows short names use the resolved file identity;
non-file URIs retain their own identity. Module paths come from the project model
or the compiler's source-path rules within workspace roots. Other buffers receive
private module identities.

Each open URI has its own text and client version. Stale changes are ignored;
reopening starts a new version sequence. Workspace versions use a separate source
counter. Closing one alias preserves the others. Closing the last reloads project
disk content or removes a standalone or deleted document.

Differing alias contents publish `CV-DOCUMENT-CONFLICT` for each buffer. Queries
use the requested buffer's text and reject conflicts in other selected files.
Rename and code actions reject any conflict in their selection. Document symbols,
folding, and formatting use the requested buffer. The conflict clears when the
remaining open buffers have matching contents.

Project mappings take precedence over loose-file module identities. Standard
classification preserves package identity and limits refactoring to project files.
Workspace receives source values; this process owns filesystem loading.

Failed project queries clear their targets while retaining known watch paths for
repair. Successful queries, including empty projects, replace the previous model.
Source edits refresh disk content; creation, deletion, and script changes refresh
the target model. Diagnostics describe unopened sources as well as open overlays.

## Private pipe protocol

Stdin and stdout carry length-prefixed records. A four-byte big-endian length
bounds each body to 16 MiB. Records contain fixed-width numbers, counted UTF-8
strings/lists, and presence flags. Requests run serially with one reply each.
Expected Workspace revisions reject queries against a different semantic state.
The LSP owns client request IDs and cancellation.

`src/protocol.cppm` defines method identities and request/result structures;
`src/protocol.cpp` and `tools/lsp/wire.cv` encode the two endpoints. The endpoints
ship together and check a private protocol version at startup. Record layout or
enum-order changes require updating both endpoints and that version.

Source-bearing responses include captured text for position conversion. Locations
refer to document identities and half-open byte ranges; compiler construction IDs
stay inside the analyzer. Open-document locations use a client URI; diagnostics
carry each open buffer's URI and version.
Workspace retains semantic analysis and recovery owners across related queries.

Workspace captures static output and denies host environment effects during
analysis. Protocol stdout contains only records; logs and startup failures use
stderr. EOF between records closes the session; truncated records fail. Active
analysis completes before the shutdown handshake.
