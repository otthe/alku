-module(factorial).
-export([main/0]).

fact(0) -> 1;
fact(N) when N > 0 -> N * fact(N - 1).

main() ->
    io:write(fact(5)), io:nl(),
    io:write(fact(20)), io:nl().
