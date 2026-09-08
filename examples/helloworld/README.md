# Hello World

Read `main.cv`, then run from the repository root:

```sh
./xmakew build
./xmakew build carven-example-hello-world
./xmakew run carven-example-hello-world
```

Expected output:

```text
Hello World
```

`import <cstdio> using std::printf;` imports the C++ standard output function.
The `c"..."` literal supplies the `const char*` that `printf` requires; an ordinary
Carven string is a `str`, represented in C++ by `std::string_view`.
No C++ helper or embedded source fragment is needed.

The examples test group checks this program's actual output.

Next: [Receipt](../basics/).
