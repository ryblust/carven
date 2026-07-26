module carven:support.visit;

template<typename... Ts>
struct Overloaded final : Ts... {
    using Ts::operator()...;
};

template<typename... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;
