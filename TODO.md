1. doc comment token `//! ///`

2. `zero run` single file

3. inline test in source code

    Proposal Syntax
    - `#test "test C/C++ like preprocessor" {}`
    - `#[test] "test rust-like attribute" {}`
    - `test "test is a keyword" {}`
    - `[[test]] fn test_cpp_attribute_like {}`

4. tests for transpiler

5. `if` expression

6. `match` expression

7. inline C++ source code

    Proposal Syntax
    - `#cpp {}`
    - `#[cpp] {}`
    - `[[cpp]] {}`