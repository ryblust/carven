1. tests for transpiler

2. doc comment token `//! ///`

3. `if` expression

4. `match` expression

5. inline test in source code

    Proposal Syntax
    - `#test "test C/C++ like preprocessor" {}`
    - `#[test] "test rust-like attribute" {}`
    - `test "test is a keyword" {}`
    - `[[test]] fn test_cpp_attribute_like {}`


6. inline C++ source code

    Proposal Syntax
    - `#cpp {}`
    - `#[cpp] {}`
    - `[[cpp]] {}`
