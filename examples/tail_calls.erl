-module(tail_calls).
-export([main/0]).

%% Each loop below runs millions of times. Without proper tail calls they
%% would overflow the stack.

count(0, Acc) -> Acc;
count(N, Acc) -> count(N - 1, Acc + 1).

is_even(0) -> true;
is_even(N) -> is_odd(N - 1).

is_odd(0) -> false;
is_odd(N) -> is_even(N - 1).

countdown(N) ->
    case N of
        0 -> done;
        _ -> countdown(N - 1)
    end.

apply_n(_, 0, X) -> X;
apply_n(F, N, X) -> apply_n(F, N - 1, F(X)).

all_positive([]) -> true;
all_positive([X | Xs]) -> X > 0 andalso all_positive(Xs).

%% Not a tail call: needs one stack frame per element.
len([]) -> 0;
len([_ | T]) -> 1 + len(T).

main() ->
    io:format("~p~n", [count(100000000, 0)]),
    io:format("~p~n", [is_even(20000000)]),
    io:format("~p~n", [countdown(10000000)]),
    Loop = fun(_, 0) -> done; (Self, N) -> Self(Self, N - 1) end,
    io:format("~p~n", [Loop(Loop, 10000000)]),
    Down = fun D(0) -> zero; D(N) -> D(N - 1) end,
    io:format("~p~n", [Down(10000000)]),
    io:format("~p~n", [apply_n(fun(X) -> X + 1 end, 1000000, 0)]),
    io:format("~p~n", [all_positive(lists:duplicate(200000, 1))]),
    io:format("~p~n", [len(lists:seq(1, 200000))]).
