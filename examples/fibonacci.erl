-module(fibonacci).
-export([main/0]).

fib(N) ->
    case N of
        0 -> 0;
        1 -> 1;
        _ -> fib(N - 1) + fib(N - 2)
    end.

main() ->
    io:write(fib(20)),
    io:nl().
