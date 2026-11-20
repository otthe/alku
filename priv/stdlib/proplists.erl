%% The proplists module of Alku's standard library, written in Alku.
%%
%% A property list is a list of {Key, Value} tuples; a plain atom A means
%% {A, true}.
-module(proplists).

-export([delete/2, from_map/1, get_all_values/2, get_bool/2, get_keys/1,
         get_value/2, get_value/3, is_defined/2, lookup/2, lookup_all/2, to_map/1]).

get_value(Key, List) -> get_value(Key, List, undefined).

get_value(Key, [Key | _], _) when is_atom(Key) -> true;
get_value(Key, [P | Ps], Default) when is_tuple(P), tuple_size(P) >= 1, element(1, P) =:= Key ->
    case P of
        {_, Value} -> Value;
        _ -> get_value(Key, Ps, Default)
    end;
get_value(Key, [_ | Ps], Default) -> get_value(Key, Ps, Default);
get_value(_, [], Default) -> Default.

get_bool(Key, List) -> get_value(Key, List, false) =:= true.

%% The first {Key, ...} entry, or none.
lookup(Key, [Key | _]) when is_atom(Key) -> {Key, true};
lookup(Key, [P | _]) when is_tuple(P), tuple_size(P) >= 1, element(1, P) =:= Key -> P;
lookup(Key, [_ | Ps]) -> lookup(Key, Ps);
lookup(_, []) -> none.

lookup_all(Key, List) ->
    [normalize(P) || P <- List, has_key(Key, P)].

get_all_values(Key, List) ->
    [V || {_, V} <- lookup_all(Key, List)].

is_defined(Key, List) -> lists:any(fun(P) -> has_key(Key, P) end, List).

delete(Key, List) -> [P || P <- List, not has_key(Key, P)].

%% Every key, once, in the order they first appear.
get_keys(List) ->
    lists:uniq([key(P) || P <- List, is_atom(P) orelse (is_tuple(P) andalso tuple_size(P) >= 1)]).

from_map(Map) -> maps:to_list(Map).

%% Earlier entries win, as with get_value/2.
to_map(List) ->
    lists:foldr(fun(P, M) ->
                        case normalize(P) of
                            {K, V} -> M#{K => V};
                            _ -> M
                        end
                end, #{}, List).

has_key(Key, Key) when is_atom(Key) -> true;
has_key(Key, P) when is_tuple(P), tuple_size(P) >= 1 -> element(1, P) =:= Key;
has_key(_, _) -> false.

key(P) when is_atom(P) -> P;
key(P) -> element(1, P).

normalize(P) when is_atom(P) -> {P, true};
normalize(P) -> P.
