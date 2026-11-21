-module(dynamic_calls).
-export([main/0, double/1, greet/2, countdown/1]).

double(X) -> X * 2.

greet(Name, Punctuation) -> "Hello, " ++ Name ++ Punctuation.

%% apply/3 in tail position is a tail call: a million steps, no stack growth.
countdown(0) -> done;
countdown(N) -> apply(?MODULE, countdown, [N - 1]).

main() ->
    M = ?MODULE,
    io:format("~p~n", [M:double(21)]),
    F = greet,
    io:format("~s~n", [M:F("Alku", "!")]),
    io:format("~p~n", [apply(lists, reverse, [[1, 2, 3]])]),
    io:format("~p~n", [apply(erlang, element, [2, {a, b, c}])]),
    Mod = lists,
    io:format("~p~n", [Mod:map(fun ?MODULE:double/1, [1, 2, 3])]),
    Double = fun M:double/1,
    io:format("~p ~p~n", [Double(5), Double]),
    Name = double,
    Arity = 1,
    io:format("~p~n", [lists:map(fun M:Name/Arity, [10, 20])]),
    io:format("~p~n", [countdown(1000000)]),
    try M:missing(1) of
        _ -> io:format("no error?~n")
    catch
        error:undef -> io:format("undef caught~n")
    end.
