# Stream collector

Collect newline-framed records from three concurrent loopback TCP connections.
The first publishes valid log records, the second closes with a truncated record,
and the third remains open without publishing. Each feed has one second to
complete reception through EOF. Rejection and timeout are handled independently
for each feed.

```sh
./xmakew run carven-example-stream-collector
./xmakew run carven-example-stream-collector --records
./xmakew run -q carven-example-stream-collector --stdin --records < application.log
```

The default output is:

```text
Collected 3 records; rejected feeds: 1; timed out: 1
INFO 12 collector connected
WARN 37 mixer token=demo clipped=2
ERROR 42 upstream timed out
```

[`workflow.cv`](workflow.cv) owns concurrent readers and publishers, bounded
record framing, UTF-8 validation, typed failures, and deadline recovery. Records
are limited to 256 bytes excluding LF; empty records are ignored. EOF requires a
complete final record. A failed feed contributes no records. Feed ordering is
stable regardless of readiness order.

[`main.cpp`](main.cpp) owns the connections and drives the exported Carven async
workflow. With `--stdin`, it first calls the exported Carven `read_input`
operation, which uses `stdin().read_all(16 * 1024 * 1024)` and the standard
`IOError` enum. It retains raw bytes for per-feed UTF-8 validation.
[`network.cpp`](network.cpp) implements the imported async receive/send providers.
[`network.hpp`](network.hpp) supplies an `EventSource` using `select` and platform
socket APIs. Reads use seven-byte buffers, while publishers send at most five
bytes per write. Framing accepts records spanning multiple reads.

This is bidirectional interoperation: C++ calls an exported async Carven function,
and Carven awaits C++ providers returning `AsyncOperation` with `IOError`.
The contexts, socket handles, and native buffers remain alive until cancellation
is acknowledged and the children close. The driver borrows the network event
source through workflow completion and detaches it before host resources are
released.

The application owns its socket layer. The example uses nonblocking TCP on
`127.0.0.1`, requires no remote service, and supports POSIX sockets and Winsock.
The Xmake target links `ws2_32` on Windows.
