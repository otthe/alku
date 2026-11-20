-module(modules).
-export([main/0]).
-import(shapes, [area/1]).

%% Uses shapes.erl, which uses geometry_util.erl: alku finds both next to
%% this file.
main() ->
    Shapes = [{circle, 1.0}, {square, 2}, {rectangle, 2, 3}],
    io:format("~p~n", [[area(S) || S <- Shapes]]),
    io:format("~p~n", [shapes:total_area(Shapes)]),
    io:format("~s~n", [shapes:describe({square, 2})]),
    io:format("~p~n", [lists:map(fun shapes:area/1, Shapes)]).
