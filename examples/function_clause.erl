-module(function_clause).
-export([main/0]).

only_small(N) when N < 3 -> N.

main() ->
    io:write(only_small(1)), io:nl(),
    io:write(only_small(5)), io:nl().
