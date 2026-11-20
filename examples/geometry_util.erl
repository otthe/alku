-module(geometry_util).
-export([sum/1]).

sum(Numbers) -> lists:foldl(fun add/2, 0, Numbers).

%% Not exported: only this module can call it.
add(X, Acc) -> X + Acc.
