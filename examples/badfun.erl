-module(badfun).
-export([main/0]).

main() ->
    F = fun(X) -> X end,
    io:format("~p~n", [F(1)]),
    F(1, 2).
