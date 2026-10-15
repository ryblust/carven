# TCP networking

[Language](README.md) · [Standard capabilities](standard.md)

`std::net` provides `ConnectionView`, `NetError`, and cold operations
over connections supplied by a native host:

| Function | Contract |
| --- | --- |
| `read(view, max_bytes)` | Require a positive limit; return `Read<Vector<u8>>::Data` with at least one byte, or `End` for normal receive EOF |
| `write(view, &&bytes)` | Consume the buffer and return a possibly short byte count; empty input succeeds with zero |
| `write_all(view, &&bytes)` | Consume the buffer, retain it through short writes, and complete only after all bytes have been sent |

Write buffers transfer ownership into the cold operation and remain alive through
completion or cancellation. Reads deliver byte chunks; callers frame records and
validate their encoding as needed.

`NetError` distinguishes `System(i32)`, `Closed`, `Conflict`, and `NoProgress`.
Cancellation has the runtime's separate completion state. One read and one write
may be active simultaneously on each native connection. A second operation in
the same direction fails with `Conflict`. A nonempty write_all with zero progress
fails with `NoProgress`. An empty write performs no native operation.

## Native host contract

Include `<carven/std/net/native.hpp>`. Create a `carven::standard::net::Network`, then adopt
connected TCP sockets into address-stable `carven::standard::net::Connection` objects.
Adoption configures nonblocking I/O; inspect `startup_error()` on the network and
each connection. A connection owns its socket, is noncopyable and immovable, and
closes exactly once. `close()` is idempotent after operations finish and reports
`Conflict` while a read or write is pending. `shutdown_send()` sends an EOF after
pending writes finish. Destruction requires no pending operations.

Use the generated `<carven/api/crafts/carven/std/net.hpp>` and
`carven::api::crafts::carven::std::net::borrow({&connection})` to construct
a Carven `ConnectionView`. Bind the view to a named variable. A null native view
reports `Closed` when used for I/O. The source holder is checked across cold and
active operations; the host must additionally retain its pointed-to connection,
network, and hidden native state until every provider has finished, including
cancellation and close. Copying a view does not copy or own a socket.

Pass the Network as the explicit EventSource to the same-thread `drive_root`.
It composes with runtime timers and input events. Finish children before destroying
views, connections, or the network.

POSIX uses poll and suppresses SIGPIPE per send (`MSG_NOSIGNAL`) or per socket
(`SO_NOSIGPIPE`); it does not change process signal handlers. Windows uses WSAPoll;
Network owns WSAStartup/WSACleanup. The Carven Xmake rule configures `ws2_32` for
both native producers and final consumers. Native sources are collected from the
installed Crafts inventory and compile as C++20.

The source API borrows host connections; it does not create connections or
listeners, resolve names, or provide TLS. Its native calls require native
execution. The native host owns network initialization and cleanup.
