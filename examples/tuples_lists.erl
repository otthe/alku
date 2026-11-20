-module(tuples_lists).
-export([main/0]).

sum([]) -> 0;
sum([H | T]) -> H + sum(T).

double_all([]) -> [];
double_all([H | T]) -> [H * 2 | double_all(T)].

swap({A, B}) -> {B, A}.

area({square, Side}) -> Side * Side;
area({rectangle, W, H}) -> W * H.

main() ->
    L = [1, 2, 3, 4],
    show(sum(L)),
    show(double_all(L)),
    show(swap({left, right})),
    show([area({square, 3}), area({rectangle, 2, 5})]),
    show(length(L)),
    show([hd(L), tl(L)]),
    show(L ++ [5, 6]),
    show([1, 2, 3, 2, 1] -- [2, 1]),
    show(lists:reverse(L)),
    show(element(2, {a, b, c})),
    show(setelement(1, {a, b}, z)),
    show(tuple_size({})),
    show([1 | 2]),
    show({[], [[]], {}}),
    show([a] < [a, b]),
    show({1, 2} < {0, 0, 0}),
    show(is_list([]) andalso is_tuple({x})).

show(Term) ->
    io:write(Term),
    io:nl().
