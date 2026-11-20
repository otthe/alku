-module(numbers).
-export([main/0]).

fact(0) -> 1;
fact(N) -> N * fact(N - 1).

fib(N) -> fib(N, 0, 1).

fib(0, A, _) -> A;
fib(N, A, B) -> fib(N - 1, B, A + B).

main() ->
    %% integers grow as large as needed
    io:format("~p~n", [fact(30)]),
    io:format("~p~n", [fib(200)]),
    io:format("~p ~p~n", [fact(25) div fact(23), fact(40) rem 1000000007]),
    io:format("~p~n", [-fact(22) div 7]),
    io:format("~p ~p~n", [9223372036854775807 + 1, -9223372036854775808 - 1]),
    io:format("~p~n", [123456789012345678901234567890 * 987654321098765432109876543210]),
    io:format("~p~n", [(1 bsl 100) - 1]),
    io:format("~p ~p ~p ~p ~p~n", [(1 bsl 100) bsr 98, -5 bsr 1, 6 band 3, 6 bor 3, 6 bxor 3]),
    io:format("~p ~p~n", [bnot 5, (1 bsl 70) band ((1 bsl 70) - 1)]),
    io:format("~p~n", [length(integer_to_list(fact(100)))]),
    io:format("~p~n", [list_to_integer("-123456789012345678901234567890")]),
    io:format("~p ~s~n", [fact(20) < fact(21) andalso fact(21) > 1.0e10, integer_to_list(255, 16)]),
    %% floats
    io:format("~p ~p ~p~n", [7 / 2, 1.5 + 1, 0.1 + 0.2]),
    io:format("~p ~p ~p ~p~n", [1000.0, 100.0, 0.0001, 1.0e-5]),
    io:format("~p ~p ~p~n", [123456789.0, 1.0e20, -2.5]),
    io:format("~p ~p ~p ~p~n", [round(2.5), trunc(-2.7), floor(-2.5), ceil(2.1)]),
    io:format("~p ~p ~p~n", [float(3), math:sqrt(2), math:pi()]),
    io:format("~p ~p~n", [math:pow(2, 10), round(math:pow(2, 70))]),
    io:format("~.2f ~f ~e~n", [math:pi(), 1.5, 123.456]),
    io:format("[~5B] [~-5B] [~5.2f] [~2..0B] [~8.3.0f]~n", [42, 42, 3.14159, 7, 2.5]),
    io:format("~.16B ~.2B ~10s|~-6w|~3w|~n", [255, 5, "right", left, 12345]),
    io:format("~p ~p~n", [1 == 1.0, 1 =:= 1.0]),
    io:format("~p~n", [lists:sort([3, 1.5, 2, 0.5])]),
    io:format("~s ~s ~p~n", [float_to_list(0.1, [short]), float_to_list(1.0, [{decimals, 3}]),
                            list_to_float("2.5e3")]),
    io:format("~p ~p~n", [try 1 / 0 catch error:badarith -> badarith end,
                          try math:sqrt(-1) catch error:badarith -> badarith end]).
