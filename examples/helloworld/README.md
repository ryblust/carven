# Hello World

Read `main.cv` from top to bottom. `import <cstdio> using std::printf;`
introduces the C++ standard output function. `main` calls it with a `c"..."`
literal, which supplies a `const char*`. An ordinary Carven string has type
`str`, represented in C++ by `std::string_view`.

From the repository root:

```sh
./xmakew build
./xmakew build carven-example-hello-world
./xmakew run carven-example-hello-world
```

Expected output:

```text
Hello World
```

The examples test group checks this program's actual output.
