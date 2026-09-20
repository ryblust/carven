# Execution stages

Run these commands from the repository root after building Carven:

```sh
./xmakew run carven examples/execution/main.cv
./xmakew run carven interpret examples/execution/main.cv
./xmakew run carven interpret --trace examples/execution/main.cv
./xmakew run carven compile examples/execution/main.cv --stdout
```

During semantic analysis, a `const {}` block prints `Preparing title`, the
`heading` initializer produces static text, and `const test` checks the prepared
values. Native execution then compiles and runs the generated program;
interpretation executes the published semantic operations directly. Both print:

```text
Build 0042
0 0
1 1
2 1
3 2
4 3
5 5
6 8
```

The top-level statements form the program's implicit entry. `fibonacci` is an
ordinary module function, using the same language rules in either execution mode.
The trace option reports interpreted statement locations and calls to stderr;
it does not trace the earlier constant evaluation. `compile --stdout` prints the
C++ artifacts to stdout and sends the compile-time message to stderr.
