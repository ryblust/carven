1. full tests for the zero transpiler

2. builtin array

    ```zero
    let a = [1, 2, 3];
    ```

    ```cpp
    const auto a = std::array{ 1, 2, 3 };
    ```

3. for loops and ranges
    ```zero
    for i in 1..10 {
        println("{}", i)
    }

    var datas = [1, 2, 3];

    for data in datas {
        data += 2;
    }
    ```

    ```cpp
    for (auto&& i : std::views::iota(1, 10)) {
        println("{}", i);
    }

    auto datas = std::array{1, 2, 3};

    for (auto&& data : datas ) {
        data += 2;
    }
    ```

3. `match` expression

4. doc comment token `//! ///`

5. inline test in zero

    Proposal Syntax
    - `#test "test C/C++ like preprocessor" {}`
    - `#[test] "test rust-like attribute" {}`
    - `test "test is a keyword" {}`
    - `[[test]] fn test_cpp_attribute_like {}`

6. inline C++ source code in zero

    Proposal Syntax
    - `#cpp {}`
    - `#[cpp] {}`
    - `[[cpp]] {}`
