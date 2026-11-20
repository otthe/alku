-module(stack_trace).
-export([main/0]).

inner(X) -> X + 1.
middle(X) -> inner(X) * 2.
outer(X) -> middle(X).

%% outer/1 tail-calls middle/1, so (as in Erlang) it is not in the trace.
main() ->
    try outer(oops)
    catch error:Reason:Stack ->
        io:format("caught ~p~n", [Reason]),
        [io:format("  ~p:~p/~p line ~p~n", [M, F, A, proplists:get_value(line, Loc)])
         || {M, F, A, Loc} <- Stack]
    end,
    outer(still_oops).
