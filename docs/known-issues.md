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
