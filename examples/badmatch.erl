-module(badmatch).
-export([main/0]).

open(Name) -> {error, Name}.

main() ->
    io:format("before~n"),
    {ok, _} = open(missing),
    io:format("after~n").
