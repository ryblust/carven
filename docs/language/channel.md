# Asynchronous byte channels

`std::channel::ByteChannel` exchanges owned byte messages between tasks on one
async driver. Handles share the channel state and retain it through pending
operations.
Channel operations support explicit static execution. Their state belongs to the
executing tasks; handles cannot be retained as module constants.

```carven
import std::channel using ByteChannel;
import std::io using ReadResult;

let channel = ByteChannel::new(2);
assert(await channel.send(&&[1u8, 2u8]));
match await channel.receive() {
    .Data(bytes) => assert(bytes.length() == 2),
    .End => assert(false),
}
channel.close();
```

`new(capacity)` requires a positive message capacity. `send(&&bytes)` returns a
cold `Operation<bool>`; it waits while full and returns false when closed.
`receive()` returns a cold `Operation<ReadResult<Vector<u8>>>`; it waits while empty.
Messages are delivered in queue order. Empty messages remain `Data([])`.
The capacity counts messages; applications bound each payload separately.

`close()` seals the channel and wakes parked operations. Receivers drain accepted
messages before `End`. Repeated close calls are allowed. Cancelling an uncommitted
send or receive removes only that operation; other waiters and queued messages
remain available. A completion already committed may win a cancellation race.

`try_send(&&bytes)` returns false when full or closed and consumes the supplied
payload in either case. `try_receive()` returns `ChannelReadResult` without
suspending: `Data(bytes)` carries a message, `Pending` means the open queue is
empty, and `End` means the channel is closed and drained. Empty messages remain
`Data([])`. These are host operations; channel handles must stay on one driver
thread, including native consumers.
