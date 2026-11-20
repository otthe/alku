-module(gc_churn).
-export([main/0]).

%% Builds and throws away about 100 MB of lists; the garbage collector
%% keeps memory use small.

churn(0, Total) -> Total;
churn(N, Total) ->
    List = lists:seq(1, 1000),
    Pairs = lists:zip(List, lists:reverse(List)),
    churn(N - 1, Total + length(Pairs)).

main() ->
    io:format("~p~n", [churn(1000, 0)]).
