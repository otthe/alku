%% The lists module of Alku's standard library, written in Alku.
%% lists:reverse/1,2 are built into the C runtime.
%%
%% Loops are tail-recursive with accumulators where it matters, so they run
%% in constant stack space on long lists.
-module(lists).

-export([all/2, any/2, append/1, append/2, concat/1, delete/2, droplast/1,
         dropwhile/2, duplicate/2, enumerate/1, filter/2, filtermap/2, flatmap/2,
         flatten/1, foldl/3, foldr/3, foreach/2, join/2, keydelete/3, keyfind/3,
         keymember/3, keyreplace/4, keysort/2, keystore/4, keytake/3, last/1, map/2,
         mapfoldl/3, max/1, member/2, min/1, nth/2, nthtail/2, partition/2,
         prefix/2, search/2, seq/2, seq/3, sort/1, sort/2, split/2, splitwith/2,
         sublist/2, sublist/3, subtract/2, suffix/2, sum/1, takewhile/2, uniq/1,
         unzip/1, usort/1, zip/2, zipwith/3]).

all(Pred, [X | Xs]) -> Pred(X) andalso all(Pred, Xs);
all(_, []) -> true.

any(Pred, [X | Xs]) -> Pred(X) orelse any(Pred, Xs);
any(_, []) -> false.

%% append([[1], [2, 3]]) is [1, 2, 3]
append(Lists) -> foldr(fun(L, Acc) -> L ++ Acc end, [], Lists).

append(A, B) -> A ++ B.

%% concat([file, ".", 3]) is "file.3"
concat(Things) -> append([thing_to_list(T) || T <- Things]).

thing_to_list(T) when is_atom(T) -> atom_to_list(T);
thing_to_list(T) when is_integer(T) -> integer_to_list(T);
thing_to_list(T) when is_float(T) -> float_to_list(T);
thing_to_list(T) when is_list(T) -> T.

%% Removes the first element that matches X.
delete(X, [X | Xs]) -> Xs;
delete(X, [Y | Ys]) -> [Y | delete(X, Ys)];
delete(_, []) -> [].

droplast([_]) -> [];
droplast([X | Xs]) -> [X | droplast(Xs)].

dropwhile(Pred, [X | Xs] = List) ->
    case Pred(X) of
        true -> dropwhile(Pred, Xs);
        false -> List
    end;
dropwhile(_, []) -> [].

duplicate(N, X) when is_integer(N), N >= 0 -> duplicate(N, X, []).

duplicate(0, _, Acc) -> Acc;
duplicate(N, X, Acc) -> duplicate(N - 1, X, [X | Acc]).

%% enumerate([a, b]) is [{1, a}, {2, b}]
enumerate(List) -> enumerate(List, 1, []).

enumerate([X | Xs], I, Acc) -> enumerate(Xs, I + 1, [{I, X} | Acc]);
enumerate([], _, Acc) -> lists:reverse(Acc).

filter(Pred, List) -> filter(Pred, List, []).

filter(_, [], Acc) -> lists:reverse(Acc);
filter(Pred, [X | Xs], Acc) ->
    case Pred(X) of
        true -> filter(Pred, Xs, [X | Acc]);
        false -> filter(Pred, Xs, Acc)
    end.

%% Fun returns true (keep), false (drop) or {true, NewValue}.
filtermap(Fun, List) -> filtermap(Fun, List, []).

filtermap(_, [], Acc) -> lists:reverse(Acc);
filtermap(Fun, [X | Xs], Acc) ->
    case Fun(X) of
        true -> filtermap(Fun, Xs, [X | Acc]);
        {true, Y} -> filtermap(Fun, Xs, [Y | Acc]);
        false -> filtermap(Fun, Xs, Acc)
    end.

flatmap(Fun, List) -> append(map(Fun, List)).

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

%% join(", ", ["a", "b"]) is ["a", ", ", "b"]
join(_, []) -> [];
join(_, [X]) -> [X];
join(Sep, [X | Xs]) -> [X, Sep | join(Sep, Xs)].

%% The key functions look at element N of tuples, comparing with ==.
keyfind(Key, N, [T | _]) when is_tuple(T), tuple_size(T) >= N, element(N, T) == Key -> T;
keyfind(Key, N, [_ | Ts]) -> keyfind(Key, N, Ts);
keyfind(_, _, []) -> false.

keymember(Key, N, List) -> keyfind(Key, N, List) =/= false.

keydelete(Key, N, [T | Ts]) when is_tuple(T), tuple_size(T) >= N, element(N, T) == Key -> Ts;
keydelete(Key, N, [T | Ts]) -> [T | keydelete(Key, N, Ts)];
keydelete(_, _, []) -> [].

keyreplace(Key, N, [T | Ts], New) when is_tuple(T), tuple_size(T) >= N, element(N, T) == Key ->
    [New | Ts];
keyreplace(Key, N, [T | Ts], New) -> [T | keyreplace(Key, N, Ts, New)];
keyreplace(_, _, [], _) -> [].

%% Replaces the first tuple with the key, or adds New at the end.
keystore(Key, N, [T | Ts], New) when is_tuple(T), tuple_size(T) >= N, element(N, T) == Key ->
    [New | Ts];
keystore(Key, N, [T | Ts], New) -> [T | keystore(Key, N, Ts, New)];
keystore(_, _, [], New) -> [New].

%% {value, Tuple, ListWithoutIt} or false
keytake(Key, N, List) -> keytake(Key, N, List, []).

keytake(Key, N, [T | Ts], Acc) when is_tuple(T), tuple_size(T) >= N, element(N, T) == Key ->
    {value, T, lists:reverse(Acc, Ts)};
keytake(Key, N, [T | Ts], Acc) -> keytake(Key, N, Ts, [T | Acc]);
keytake(_, _, [], _) -> false.

%% Stable: tuples with equal keys keep their order.
keysort(N, List) -> sort(fun(A, B) -> element(N, A) =< element(N, B) end, List).

last([X]) -> X;
last([_ | Xs]) -> last(Xs).

map(F, List) -> map(F, List, []).

map(_, [], Acc) -> lists:reverse(Acc);
map(F, [X | Xs], Acc) -> map(F, Xs, [F(X) | Acc]).

%% Like map, while threading an accumulator: F(X, Acc) -> {Y, NewAcc}.
mapfoldl(F, Acc, List) -> mapfoldl(F, Acc, List, []).

mapfoldl(F, Acc, [X | Xs], Ys) ->
    {Y, Acc1} = F(X, Acc),
    mapfoldl(F, Acc1, Xs, [Y | Ys]);
mapfoldl(_, Acc, [], Ys) -> {lists:reverse(Ys), Acc}.

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

nthtail(0, List) -> List;
nthtail(N, [_ | Xs]) when N > 0 -> nthtail(N - 1, Xs).

%% {Satisfying, NotSatisfying}
partition(Pred, List) -> partition(Pred, List, [], []).

partition(Pred, [X | Xs], Yes, No) ->
    case Pred(X) of
        true -> partition(Pred, Xs, [X | Yes], No);
        false -> partition(Pred, Xs, Yes, [X | No])
    end;
partition(_, [], Yes, No) -> {lists:reverse(Yes), lists:reverse(No)}.

prefix([X | Xs], [X | Ys]) -> prefix(Xs, Ys);
prefix([], _) -> true;
prefix(_, _) -> false.

%% {value, X} for the first X that satisfies Pred, or false
search(Pred, [X | Xs]) ->
    case Pred(X) of
        true -> {value, X};
        false -> search(Pred, Xs)
    end;
search(_, []) -> false.

seq(From, To) when is_integer(From), is_integer(To), From =< To + 1 ->
    seq_down(To, From, []).

seq_down(N, From, Acc) when N >= From -> seq_down(N - 1, From, [N | Acc]);
seq_down(_, _, Acc) -> Acc.

%% seq(1, 10, 3) is [1, 4, 7, 10]
seq(From, To, Incr) when is_integer(From), is_integer(To), is_integer(Incr),
                         (Incr > 0 andalso From - Incr =< To) orelse
                         (Incr < 0 andalso From - Incr >= To) orelse
                         (Incr =:= 0 andalso From =:= To) ->
    Count = case Incr of
                0 -> 1;
                _ -> (To - From) div Incr + 1
            end,
    seq_by(From + (Count - 1) * Incr, Incr, Count, []).

seq_by(_, _, 0, Acc) -> Acc;
seq_by(X, Incr, Count, Acc) -> seq_by(X - Incr, Incr, Count - 1, [X | Acc]).

%% Merge sort in Erlang term order.
sort([]) -> [];
sort([X]) -> [X];
sort(List) ->
    {A, B} = halves(List, [], []),
    merge(sort(A), sort(B), []).

halves([], A, B) -> {A, B};
halves([X | Xs], A, B) -> halves(Xs, B, [X | A]).

merge([A | As], [B | Bs], Acc) when A =< B -> merge(As, [B | Bs], [A | Acc]);
merge(As, [B | Bs], Acc) when As =/= [] -> merge(As, Bs, [B | Acc]);
merge([], Bs, Acc) -> lists:reverse(Acc, Bs);
merge(As, [], Acc) -> lists:reverse(Acc, As).

%% Stable merge sort; Ordered(A, B) is true when A should not come after B.
sort(_, []) -> [];
sort(_, [X]) -> [X];
sort(Ordered, List) ->
    {A, B} = split(erlang:length(List) div 2, List),
    merge_by(Ordered, sort(Ordered, A), sort(Ordered, B), []).

merge_by(Ordered, [A | As], [B | Bs], Acc) ->
    case Ordered(A, B) of
        true -> merge_by(Ordered, As, [B | Bs], [A | Acc]);
        false -> merge_by(Ordered, [A | As], Bs, [B | Acc])
    end;
merge_by(_, [], Bs, Acc) -> lists:reverse(Acc, Bs);
merge_by(_, As, [], Acc) -> lists:reverse(Acc, As).

%% {FirstN, Rest}
split(N, List) when is_integer(N), N >= 0 -> split_at(N, List, []).

split_at(0, List, Acc) -> {lists:reverse(Acc), List};
split_at(N, [X | Xs], Acc) -> split_at(N - 1, Xs, [X | Acc]).

%% {LongestPrefixSatisfyingPred, Rest}
splitwith(Pred, List) -> splitwith(Pred, List, []).

splitwith(Pred, [X | Xs], Acc) ->
    case Pred(X) of
        true -> splitwith(Pred, Xs, [X | Acc]);
        false -> {lists:reverse(Acc), [X | Xs]}
    end;
splitwith(_, [], Acc) -> {lists:reverse(Acc), []}.

%% The first Len elements (or fewer).
sublist(List, Len) when is_integer(Len), Len >= 0 -> take(List, Len, []).

take([X | Xs], N, Acc) when N > 0 -> take(Xs, N - 1, [X | Acc]);
take(_, _, Acc) -> lists:reverse(Acc).

%% Len elements starting at position Start (the first is 1).
sublist(List, Start, Len) when is_integer(Start), Start >= 1 ->
    sublist(nthtail(Start - 1, List), Len).

subtract(A, B) -> A -- B.

suffix(Suffix, List) ->
    Extra = erlang:length(List) - erlang:length(Suffix),
    Extra >= 0 andalso nthtail(Extra, List) =:= Suffix.

sum(List) -> sum(List, 0).

sum([X | Xs], Acc) -> sum(Xs, Acc + X);
sum([], Acc) -> Acc.

takewhile(Pred, [X | Xs]) ->
    case Pred(X) of
        true -> [X | takewhile(Pred, Xs)];
        false -> []
    end;
takewhile(_, []) -> [].

%% Removes duplicates, keeping the first of each.
uniq(List) -> uniq(List, #{}, []).

uniq([X | Xs], Seen, Acc) ->
    case Seen of
        #{X := _} -> uniq(Xs, Seen, Acc);
        _ -> uniq(Xs, Seen#{X => true}, [X | Acc])
    end;
uniq([], _, Acc) -> lists:reverse(Acc).

unzip(Pairs) -> unzip(Pairs, [], []).

unzip([{A, B} | Pairs], As, Bs) -> unzip(Pairs, [A | As], [B | Bs]);
unzip([], As, Bs) -> {lists:reverse(As), lists:reverse(Bs)}.

%% Sorted, without duplicates.
usort(List) -> dedup(sort(List)).

dedup([X, Y | Rest]) when X == Y -> dedup([X | Rest]);
dedup([X | Rest]) -> [X | dedup(Rest)];
dedup([]) -> [].

zip(As, Bs) -> zip(As, Bs, []).

zip([A | As], [B | Bs], Acc) -> zip(As, Bs, [{A, B} | Acc]);
zip([], [], Acc) -> lists:reverse(Acc).

zipwith(F, [A | As], [B | Bs]) -> [F(A, B) | zipwith(F, As, Bs)];
zipwith(_, [], []) -> [].
