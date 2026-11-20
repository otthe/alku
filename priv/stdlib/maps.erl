%% The higher-order part of Alku's maps module, written in Alku.
%% The basic operations (get, put, remove, keys, to_list, ...) are built
%% into the C runtime.
-module(maps).

-export([filter/2, fold/3, foreach/2, map/2, update_with/3, update_with/4,
         with/2, without/2]).

%% F(Key, Value, Acc) for every entry, in key order.
fold(F, Acc, Map) ->
    lists:foldl(fun({K, V}, A) -> F(K, V, A) end, Acc, maps:to_list(Map)).

map(F, Map) ->
    maps:from_list([{K, F(K, V)} || {K, V} <- maps:to_list(Map)]).

filter(Pred, Map) ->
    maps:from_list([{K, V} || {K, V} <- maps:to_list(Map), Pred(K, V)]).

foreach(F, Map) ->
    lists:foreach(fun({K, V}) -> F(K, V) end, maps:to_list(Map)).

update_with(Key, F, Map) ->
    maps:update(Key, F(maps:get(Key, Map)), Map).

%% Like update_with/3, but a missing key gets Init.
update_with(Key, F, Init, Map) ->
    case maps:find(Key, Map) of
        {ok, V} -> maps:put(Key, F(V), Map);
        error -> maps:put(Key, Init, Map)
    end.

with(Keys, Map) ->
    maps:from_list([{K, V} || K <- Keys, {ok, V} <- [maps:find(K, Map)]]).

without(Keys, Map) ->
    lists:foldl(fun(K, M) -> maps:remove(K, M) end, Map, Keys).
