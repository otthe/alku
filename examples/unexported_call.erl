-module(unexported_call).
-export([main/0]).

main() -> io:format("~p~n", [geometry_util:add(1, 2)]).
