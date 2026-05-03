0. refactor driver layer

    Extract logic from pipeline and cli

1. `zero run` single file

    implement @src/driver/pipeline.cppm: `Driver::run_single_file`
    1. Check if given source file is modified if so delete build cache in the `Driver::output_dir`
    2. Transpile given .zero source file to .cpp file and output to the given `Driver::output_dir`
    3. Call the C++ Compiler to compile to a executable and output to the given `Driver::output_dir`
    4. Run the executable

2. tests for transpiler

3. doc comment token `//! ///`

4. `if` expression

5. `match` expression

6. inline test in source code

    Proposal Syntax
    - `#test "test C/C++ like preprocessor" {}`
    - `#[test] "test rust-like attribute" {}`
    - `test "test is a keyword" {}`
    - `[[test]] fn test_cpp_attribute_like {}`


7. inline C++ source code

    Proposal Syntax
    - `#cpp {}`
    - `#[cpp] {}`
    - `[[cpp]] {}`