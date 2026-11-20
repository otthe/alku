-module(comprehensions).
-export([main/0]).

main() ->
    io:format("~p~n", [[X * X || X <- [1, 2, 3, 4]]]),
    io:format("~p~n", [[X || X <- lists:seq(1, 10), X rem 3 =:= 0]]),
    io:format("~p~n", [[{X, Y} || X <- [a, b], Y <- [1, 2]]]),
    io:format("~p~n", [[N || {ok, N} <- [{ok, 1}, error, {ok, 2}]]]),
    io:format("~p~n", [[C - 32 || C <- "abc"]]),
    Base = 1000,
    io:format("~p~n", [[Base + X || X <- [1, 2]]]),
    io:format("~p~n", [length([X || X <- lists:seq(1, 300000), X rem 2 =:= 0])]),
    io:format("~s~n", [string:to_upper("hello")]),
    io:format("~p~n", [string:tokens("a  b,c", " ,")]),
    io:format("~p~n", [string:split("key=value=x", "=")]),
    io:format("~p~n", [string:split("a,b,,c", ",", all)]),
    io:format("~p~n", [string:trim("  padded \n")]),
    io:format("~s~n", [string:join(["x", "y", "z"], ", ")]).
