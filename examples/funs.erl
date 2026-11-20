-module(funs).
-export([main/0]).

add(N) -> fun(X) -> X + N end.

compose(F, G) -> fun(X) -> F(G(X)) end.

double(X) -> X * 2.

main() ->
    Add5 = add(5),
    io:format("~p~n", [Add5(10)]),
    Inc = fun(X) -> X + 1 end,
    io:format("~p~n", [lists:map(Inc, [1, 2, 3])]),
    io:format("~p~n", [lists:map(fun double/1, [1, 2, 3])]),
    io:format("~p~n", [(compose(Inc, fun double/1))(10)]),
    io:format("~p~n", [lists:filter(fun(X) -> X rem 2 =:= 0 end, lists:seq(1, 10))]),
    io:format("~p~n", [lists:foldl(fun(X, Sum) -> X + Sum end, 0, lists:seq(1, 100))]),
    io:format("~p~n", [lists:foldr(fun(X, Acc) -> [X * X | Acc] end, [], [1, 2, 3])]),
    Fact = fun F(0) -> 1; F(N) -> N * F(N - 1) end,
    io:format("~p~n", [Fact(10)]),
    io:format("~p~n", [lists:map(fun erlang:abs/1, [-1, 2, -3])]),
    io:format("~p~n", [lists:zip([a, b, c], [1, 2, 3])]),
    io:format("~p ~p ~p ~p~n", [lists:sum([1, 2, 3]), lists:member(b, [a, b]),
                                lists:max([3, 7, 2]), lists:min([3, 7, 2])]),
    io:format("~p~n", [lists:sort([3, 1, 2, c, a, {x}, [], "s"])]),
    io:format("~p~n", [lists:flatten([1, [2, [3, []], 4]])]),
    io:format("~p ~p~n", [lists:nth(2, [a, b, c]), lists:last([a, b, c])]),
    io:format("~p ~p~n", [lists:any(fun erlang:is_atom/1, [1, a]), lists:all(fun erlang:is_atom/1, [1, a])]),
    io:format("~p~n", [is_function(Inc) andalso is_function(Inc, 1)]),
    Counter = make_counter(10),
    io:format("~p~n", [Counter(Counter(0))]),
    lists:foreach(fun(X) -> io:format("item ~p~n", [X]) end, [x, y]).

%% A closure capturing two variables, one of them another fun.
make_counter(Step) ->
    Add = fun(X, Y) -> X + Y end,
    fun(N) -> Add(N, Step) end.
