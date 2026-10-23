# Filesystem access

`std::fs` reads files in chunks or as complete values, writes files, resolves and
queries paths, lists files, and creates directories.
Operations are synchronous. Paths are UTF-8 strings; relative paths use the process
working directory. Filesystem operations reject paths containing NUL with
`FileError::System`. All functions are `const fn`. Filesystem access in static
calls requires host effects to be enabled for the analysis. `parent` and `join`
are lexical path operations and do not access the filesystem.

```carven
import std::fs using { create_directories, read_text, write_text }

create_directories("build/generated")?;
write_text("build/generated/message.txt", "hello\n")?;
let message = read_text("build/generated/message.txt")?;
```

| Function | Result |
| --- | --- |
| `read_bytes(path: str)` | `Vector<u8>` containing the complete file |
| `write_bytes(path: str, bytes: [u8])` | Creates or replaces the file from a borrowed byte slice |
| `read_text(path: str)` | `String` after UTF-8 validation |
| `write_text(path: str, text: str)` | Writes the UTF-8 bytes without newline conversion |
| `canonical(path: str)` | Absolute path with symbolic links, `.` and `..` resolved |
| `resolve(path: str)` | Absolute path resolving the existing prefix and normalizing a possibly absent suffix |
| `parent(path: str)` | Lexical parent path using host path syntax and `/` separators; empty if no parent |
| `join(base: str, path: str)` | Joins paths using host path syntax and `/` separators; an absolute path replaces the base |
| `remove(path: str)` | Removes a file, symbolic link, or empty directory; returns whether an entry was removed |
| `status(path: str)` | `PathKind::Missing`, `File`, `Directory`, or `Other` |
| `create_directories(path: str)` | Creates missing parent directories; an existing directory succeeds |
| `files(scan: DirectoryScan)` | Sorted regular-file paths selected by a directory scan |

Filesystem operations may throw `FileError`; `parent` and `join` do not throw.
`System(i32)` carries a platform error code;
`InvalidUTF8(UTF8Error)` reports invalid input to `read_text`. Byte access accepts
arbitrary file contents. Successful writes finish closing the file before returning.

## Streaming reads

`FileReader::open(path: str) -> FileReader` opens a binary stream.
`read(limit: usize) -> std::io::ReadResult<Vector<u8>>` requires a positive limit
and returns either nonempty `Data(bytes)` containing at most that many bytes or
`End`. Reads advance the cursor; repeated reads after reaching the end return
`End`. Each read uses bounded storage independently of the file size.

```carven
import std::fs using FileReader;

let reader = FileReader::open("input.bin")?;
var size = 0usize;
while {
    match reader.read(16384)? {
        .Data(bytes) => { size += bytes.length(); },
        .End => break,
    }
}
reader.close()?;
println(size);
```

Reader copies share the cursor and open state. `close()` releases the file for
all copies; repeated closes succeed, and reading after close throws
`FileError::System`. The last reader releases the file automatically on scope
exit, including error exits. Open, read and explicit close failures throw
`FileError::System`. These methods support `const` execution with host effects;
readers cannot be frozen as constants. Static execution retains its ordinary
byte and work budgets even when input is processed in chunks.

## Paths and directories

`status` follows symbolic links. A missing path, including a dangling link, returns
`Missing`; lookup failures throw. Write operations do not create parent directories.
`canonical` requires an existing path and returns valid UTF-8 with `/` separators.
`resolve` also accepts paths whose final components do not exist; it does not
create them. Both operations use the filesystem to resolve symbolic links.

`DirectoryScan` supplies the root, recursion policy, and an optional excluded
subtree as named fields:

```carven
import std::fs using files;

let sources = files({ root: "src", recursive: true, excluded_root: "" })?;
let local = files({ root: ".", recursive: false, excluded_root: "" })?;
```

A missing root returns an empty vector. Directory symlinks are not followed.
The excluded subtree need not exist yet. A nonempty `excluded_root` names a
subtree to omit, including paths resolving to that subtree. Returned paths retain the root's relative or absolute
form and use `/` separators. Filenames must be valid UTF-8.
