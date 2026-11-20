-module(exceptions).
-export([main/0]).

safe_div(A, B) ->
    try A div B of
        R -> {ok, R}
    catch
        error:badarith -> {error, division_by_zero}
    end.

find(_, []) -> throw(not_found);
find(K, [{K, V} | _]) -> V;
find(K, [_ | T]) -> find(K, T).

lookup(K, L) ->
    try find(K, L)
    catch
        throw:not_found -> default
    end.

%% A guard that raises an exception is simply false.
classify(X) when hd(X) > 0 -> positive_head;
classify(_) -> other.

cleanup() ->
    try
        io:format("working~n"),
        error(oops)
    after
        io:format("cleanup done~n")
    end.

main() ->
    io:format("~p ~p~n", [safe_div(7, 2), safe_div(1, 0)]),
    io:format("~p ~p~n", [lookup(b, [{a, 1}, {b, 2}]), lookup(z, [{a, 1}])]),
    io:format("~p ~p ~p~n", [classify([5]), classify([]), classify(x)]),
    io:format("~p~n", [(catch throw(thrown))]),
    io:format("~p~n", [(catch exit(bye))]),
    io:format("~p~n", [(catch 1 + 2)]),
    io:format("~p~n", [case catch hd([]) of {'EXIT', {badarg, _}} -> caught_badarg end]),
    R = try cleanup() catch error:Reason -> {caught, Reason} end,
    io:format("~p~n", [R]),
    io:format("~p~n", [try error({my, error}) catch Class:Reason2 -> {Class, Reason2} end]),
    io:format("~p~n", [try lists:nth(5, [a]) catch error:function_clause -> no_such_element end]),
    io:format("~p~n", [try {ok, X} = {error, 1}, X catch error:{badmatch, V} -> {badmatch, V} end]),
    error(unhandled).
