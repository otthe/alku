%% The lists module of Alku's standard library, written in Alku.
%% lists:reverse/1,2 are built into the C runtime.
%%
%% Loops are tail-recursive with accumulators, so they run in constant
%% stack space on lists of any length.
-module(lists).

-export([all/2, any/2, append/2, duplicate/2, filter/2, flatten/1, foldl/3,
         foldr/3, foreach/2, last/1, map/2, max/1, member/2, min/1, nth/2,
         seq/2, sort/1, sum/1, zip/2]).

all(Pred, [X | Xs]) -> Pred(X) andalso all(Pred, Xs);
all(_, []) -> true.

any(Pred, [X | Xs]) -> Pred(X) orelse any(Pred, Xs);
any(_, []) -> false.

append(A, B) -> A ++ B.

duplicate(N, X) when is_integer(N), N >= 0 -> duplicate(N, X, []).

duplicate(0, _, Acc) -> Acc;
duplicate(N, X, Acc) -> duplicate(N - 1, X, [X | Acc]).

filter(Pred, List) -> filter(Pred, List, []).

filter(_, [], Acc) -> lists:reverse(Acc);
filter(Pred, [X | Xs], Acc) ->
    case Pred(X) of
        true -> filter(Pred, Xs, [X | Acc]);
        false -> filter(Pred, Xs, Acc)
    end.

flatten(List) -> lists:reverse(flatten(List, [])).

%% Collects the elements of a deep list, last one first.
flatten([], Acc) -> Acc;
flatten([X | Xs], Acc) when is_list(X) -> flatten(Xs, flatten(X, Acc));
flatten([X | Xs], Acc) -> flatten(Xs, [X | Acc]).

foldl(F, Acc, [X | Xs]) -> foldl(F, F(X, Acc), Xs);
foldl(_, Acc, []) -> Acc.

foldr(F, Acc, List) -> foldl(F, Acc, lists:reverse(List)).

foreach(F, [X | Xs]) ->
    F(X),
    foreach(F, Xs);
foreach(_, []) -> ok.

last([X]) -> X;
last([_ | Xs]) -> last(Xs).

map(F, List) -> map(F, List, []).

map(_, [], Acc) -> lists:reverse(Acc);
map(F, [X | Xs], Acc) -> map(F, Xs, [F(X) | Acc]).

max([X | Xs]) -> max_of(Xs, X).

max_of([X | Xs], Best) when X > Best -> max_of(Xs, X);
max_of([_ | Xs], Best) -> max_of(Xs, Best);
max_of([], Best) -> Best.

member(X, [X | _]) -> true;
member(X, [_ | Xs]) -> member(X, Xs);
member(_, []) -> false.

min([X | Xs]) -> min_of(Xs, X).

min_of([X | Xs], Best) when X < Best -> min_of(Xs, X);
min_of([_ | Xs], Best) -> min_of(Xs, Best);
min_of([], Best) -> Best.

nth(1, [X | _]) -> X;
nth(N, [_ | Xs]) when N > 1 -> nth(N - 1, Xs).

seq(From, To) when is_integer(From), is_integer(To), From =< To + 1 ->
    seq_down(To, From, []).

seq_down(N, From, Acc) when N >= From -> seq_down(N - 1, From, [N | Acc]);
seq_down(_, _, Acc) -> Acc.

%% Merge sort in Erlang term order.
sort([]) -> [];
sort([X]) -> [X];
sort(List) ->
    {A, B} = split(List, [], []),
    merge(sort(A), sort(B), []).

split([], A, B) -> {A, B};
split([X | Xs], A, B) -> split(Xs, B, [X | A]).

merge([A | As], [B | Bs], Acc) when A =< B -> merge(As, [B | Bs], [A | Acc]);
merge(As, [B | Bs], Acc) when As =/= [] -> merge(As, Bs, [B | Acc]);
merge([], Bs, Acc) -> lists:reverse(Acc, Bs);
merge(As, [], Acc) -> lists:reverse(Acc, As).

sum(List) -> sum(List, 0).

sum([X | Xs], Acc) -> sum(Xs, Acc + X);
sum([], Acc) -> Acc.

zip(As, Bs) -> zip(As, Bs, []).

zip([A | As], [B | Bs], Acc) -> zip(As, Bs, [{A, B} | Acc]);
zip([], [], Acc) -> lists:reverse(Acc).
