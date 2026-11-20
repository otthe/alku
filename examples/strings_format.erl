-module(strings_format).
-export([main/0]).

greet(Name) -> "Hello, " ++ Name ++ "!".

initial("Alku") -> alku_detected;
initial([First | _]) -> First.

main() ->
    io:format("~s~n", [greet("Alku")]),
    io:format("~p and ~w~n", ["abc", "abc"]),
    io:format("~p~n", [{point, 3, -4}]),
    io:format("~p~n", [["one", "two", []]]),
    io:format("~p~n", ["tab\there \"quoted\""]),
    io:format("~s has ~b letters~n", [hello, length("hello")]),
    io:format("char: ~c, tilde: ~~~n", [$A]),
    io:format("no arguments~n"),
    io:format("~w~n", [[$a, 1]]),
    io:format("~p ~c~n", [initial("Alku"), initial("World")]),
    io:format("~s~n", [integer_to_list(-42) ++ " " ++ atom_to_list(ok)]).
