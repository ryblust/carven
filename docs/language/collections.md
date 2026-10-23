# Collections

`std::collections` provides `HashMap<K, V>` and `HashSet<T>`. Keys require the
built-in `Hash` and `Equal` capabilities, whose names are available by default.
Container operations are `const fn`; static, interpreted, and native execution use
the same Carven implementation.
Stored types follow the language's owning-storage rules.

## Maps

```carven
import std::collections using HashMap;

var counts = HashMap<String, usize>::new();
let key: String = "main.cv";
counts.insert(&&key, &&1);
let query: String = "main.cv";
let count = counts.find(query);
if count != nullptr { assert(*count == 1); }
```

`new()` creates an empty map. `with_capacity(capacity)` reserves space for the
expected number of entries. `length()` counts entries; `is_empty()` checks whether
there are any.

`insert(&&key: K, &&value: V) -> bool` takes ownership of both arguments. A new key
returns `true`. An existing key returns `false` and retains its original key and
value; the unused arguments are destroyed normally.

`find(key: K) -> ptr<V>` returns a read-only pointer to the stored value, or
`nullptr` when absent. `contains(key: K) -> bool` tests membership. Query arguments
use Read access, so querying an existing owning key does not copy it. The query
has the map's key type; heterogeneous query types are not provided.

`find_mut(key: K) -> ptr<&V>` takes Write access to the map and returns a writable
pointer to a found value, or `nullptr`. The key remains read-only.

`entries() -> [MapEntry<K, V>]` returns a read-only borrowed slice. Each entry has
`key` and `value` fields. A retained slice protects the map's storage under
ordinary borrow rules. Pointers returned by `find` and `find_mut` do not retain a
borrow. They require the entry to remain alive at the same address; insertion,
removal, or `clear` can invalidate them.

`remove(key: K) -> Lookup<V>` returns `.Found(value)` by transferring the stored
value, or `.Missing`. `clear()` destroys all entries. Empty construction, insertion,
lookup, and removal do not require default initialization or copying of `K` or
`V`. A map itself can be copied when its stored types support copying.

## Sets

`HashSet<T>` owns distinct keys. It provides `new()`, `with_capacity(capacity)`,
`length()`, `is_empty()`, `contains(key: T)`, and `clear()` with the same roles as
the corresponding map operations.

`insert(&&key: T) -> bool` takes ownership and returns whether a new key was added.
An existing equal key is retained. `remove(key: T) -> Lookup<T>` transfers a found
key out of the set. `values() -> [T]` returns a read-only borrowed slice.

## Hashing and storage

Equal keys must produce equal hashes. Different keys may share a hash; the
containers always check key equality when hashes match. Keys remain
read-only while stored. To change a key, remove it and insert the changed value.

Containers keep entries in contiguous storage and index them by hash. Lookup and
insertion have expected constant cost, with occasional linear growth. Removal
preserves the remaining entry sequence and is linear in the entry count and
bucket capacity. Slices expose entries in first-insertion order. Duplicate
insertion retains the existing
position; removal preserves the relative order of the remaining entries.

`hash_bytes(bytes: [u8]) -> u64` hashes a byte sequence. `hash_text(text: str) -> u64`
hashes its UTF-8 bytes. These operations do not observe the host environment.
Hash values support in-memory lookup; they do not define persistent artifact
identities.

## Incremental byte digests

`ByteDigest::new()` starts a deterministic 128-bit byte digest. `update(bytes: [u8])`
adds bytes in order; updating with several chunks produces the same digest as
updating with their concatenation. Empty updates have no effect.
`value() -> (u64, u64)` returns a snapshot and permits further updates.
Copying a digest copies its current state independently.

```carven
import std::collections using ByteDigest;

var digest = ByteDigest::new();
digest.update("hello".bytes);
digest.update(", world".bytes);
let fingerprint = digest.value();
```

These methods are `const fn`; the state and completed value can be retained as
constants. Digests serve content-change detection, not cryptographic authentication.
The algorithm is not a persistent format guarantee across toolchain versions.
Callers frame separate values when their boundaries matter.
