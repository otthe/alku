-module(maps_demo).
-export([main/0]).

describe(#{type := circle, r := R}) -> {circle, R};
describe(#{type := T}) -> T.

main() ->
    M = #{name => "Alku", version => 5},
    io:format("~p~n", [M]),
    #{name := Name} = M,
    io:format("~s~n", [Name]),
    M2 = M#{version := 6, stable => false},
    io:format("~p~n", [M2]),
    io:format("~p ~p ~p~n", [maps:get(version, M2), maps:get(missing, M2, none), map_size(M2)]),
    io:format("~p ~p~n", [maps:find(name, M2), maps:find(nope, M2)]),
    io:format("~p~n", [maps:to_list(#{b => 2, a => 1})]),
    io:format("~p~n", [maps:keys(maps:from_list([{3, c}, {1, a}, {2, b}]))]),
    Words = string:tokens("the cat the dog the end", " "),
    Counts = lists:foldl(fun(W, Acc) -> maps:update_with(W, fun(C) -> C + 1 end, 1, Acc) end,
                         #{}, Words),
    io:format("~p~n", [Counts]),
    io:format("~p~n", [maps:fold(fun(_K, V, Sum) -> V + Sum end, 0, Counts)]),
    io:format("~p~n", [maps:map(fun(_K, V) -> V * 10 end, #{a => 1, b => 2})]),
    io:format("~p~n", [maps:filter(fun(K, _V) -> K =/= a end, #{a => 1, b => 2})]),
    io:format("~p~n", [#{K => V * 2 || K := V <- #{x => 1, y => 2}}]),
    io:format("~p~n", [[K || K := _ <- #{p => 1, q => 2}]]),
    io:format("~p ~p~n", [maps:merge(#{a => 1, b => 2}, #{b => 3}), maps:remove(a, #{a => 1, b => 2})]),
    io:format("~p ~p~n", [maps:with([a, c], #{a => 1, b => 2, c => 3}), maps:without([a], #{a => 1, b => 2})]),
    Big = maps:from_list([{N, N * N} || N <- lists:seq(1, 10000)]),
    io:format("~p ~p~n", [map_size(Big), maps:get(9999, Big)]),
    io:format("~p ~p~n", [describe(#{type => circle, r => 2}), describe(#{type => square})]),
    io:format("~p~n", [is_map(M) andalso is_map_key(name, M)]),
    io:format("~p~n", [try maps:get(nope, M) catch error:{badkey, K} -> {missing, K} end]),
    io:format("~p ~p~n", [#{1 => a} =:= #{1.0 => a}, #{a => 1} == #{a => 1.0}]).
