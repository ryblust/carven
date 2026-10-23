# A preview that can be hot-reloaded

This example embeds Carven in a C++ document editor. Its preview starts as compiled
C++ generated from [preview.cv](preview.cv). The host then loads
[preview_updated.cv](preview_updated.cv) into the same session while the first
preview is waiting for a reply.

`preview.cv` uses `export(cpp)` for its compiled C++ entry. The replacement uses
bare visibility: hosting selects functions in the chosen entry module without
requiring cross-craft exports. Private helpers remain internal to that module.

Read the two short Carven functions first, then [main.cpp](main.cpp). The host
registers one compiled entry and one asynchronous provider, creates a session,
and subscribes the editor to `preview` events. The provider stores requests for
explicit completion; no network service or worker thread is needed for this demo.

The application runs one sequence:

1. Editing the document starts a compiled preview and its native query.
2. Reloading replaces the preview entry. The next edit runs interpreted source
   with the same provider, editor state, and event endpoint.
3. The newer reply arrives first and updates the view. The older call completes
   its original implementation, but the editor publishes only its latest revision.
4. Closing the editor revokes new events and cancels its pending query.

`poll` advances bounded execution slices on the host's thread. Completing a
request delivers data without entering Carven inline. The host owns the event
loop, result selection, and cancellation; reloading does not choose which result
the application should display.

From the repository root:

```sh
./xmakew build
./xmakew build carven-example-runtime
./xmakew run carven-example-runtime
./xmakew test -g runtime
```

The output shows revision 2 finishing before revision 1, the view retaining the
updated text, and the provider receiving cancellation when the editor closes.
Use `.\xmakew.ps1` in Windows PowerShell.

The host uses C++23 headers from `tools/hosting/include/carven/hosting/` and links the
`carven-hosting` static library. Its sources include the entry, request, and
service interfaces directly. The generated preview and its provider implementation
use C++20. `preview_provider.cpp` connects the generated native entry point to the
session's registered provider; both execution modes use the contract generated
from `preview.cv`.

Argument validation, failure payloads, invalid replacement, value limits, object
lifetime, and cancellation races are covered by the hosting contract tests
in `tests/interop/hosting`. They are separate from this application walkthrough.
