# Known Issues

## Arena does not destroy non-trivial AST nodes

Status: accepted for now.

Some AST nodes contain non-trivial members such as `std::vector`, but runtime
arena allocation releases raw memory blocks without running destructors for
allocated nodes. This can leak vector-owned heap allocations until process exit.

Current impact is acceptable because Carven is currently a short-lived,
single-file CLI.

Revisit this when:

- adding an LSP or daemon mode
- adding multi-file incremental compilation
- fuzzing large inputs
- keeping ASTs alive across compilation units

## Windows process argument quoting is limited

Status: accepted for now.

On Windows, process launching currently builds a command string from argument
vectors by quoting only arguments that contain spaces or tabs. This is simpler
than the full Windows command-line escaping rules and may mishandle arguments
containing quotes, backslashes before quotes, or other edge cases interpreted by
the child process runtime.

Current impact is acceptable because Carven primarily forwards simple xmake
commands and common file paths.

Revisit this when:

- forwarding arbitrary user arguments on Windows
- adding Windows e2e coverage for paths or arguments containing quotes
- relying on subprocess calls for more than xmake delegation
- replacing command-string construction with a tested Windows argument quoting
  helper
