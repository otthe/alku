%% The string module of Alku's standard library, written in Alku.
%% Strings are lists of character codes. Case conversion covers ASCII only.
-module(string).

-export([is_empty/1, join/2, lowercase/1, split/2, split/3, to_lower/1,
         to_upper/1, tokens/2, trim/1, uppercase/1]).

is_empty([]) -> true;
is_empty(_) -> false.

join([], _) -> [];
join([X], _) -> X;
join([X | Xs], Sep) -> X ++ Sep ++ join(Xs, Sep).

%% Splits at the first Sep: "a,b,c" with "," gives ["a", "b,c"].
split(String, Sep) -> split(String, Sep, leading).

%% split(String, Sep, all) splits at every Sep.
split(String, [], _) -> [String];
split(String, Sep, leading) -> split_first(String, Sep, []);
split(String, Sep, all) -> split_all(String, Sep, [], []).

split_first(String, Sep, Before) ->
    case strip_prefix(Sep, String) of
        {ok, After} -> [lists:reverse(Before), After];
        nomatch when String =:= [] -> [lists:reverse(Before)];
        nomatch -> split_first(tl(String), Sep, [hd(String) | Before])
    end.

split_all([], _, Current, Parts) ->
    lists:reverse([lists:reverse(Current) | Parts]);
split_all([C | Cs] = String, Sep, Current, Parts) ->
    case strip_prefix(Sep, String) of
        {ok, After} -> split_all(After, Sep, [], [lists:reverse(Current) | Parts]);
        nomatch -> split_all(Cs, Sep, [C | Current], Parts)
    end.

strip_prefix([], Rest) -> {ok, Rest};
strip_prefix([C | Prefix], [C | Rest]) -> strip_prefix(Prefix, Rest);
strip_prefix(_, _) -> nomatch.

%% Splits at any of the separator characters, dropping empty tokens:
%% tokens("a  b,c", " ,") gives ["a", "b", "c"].
tokens(String, Seps) -> tokens(String, Seps, [], []).

tokens([], _, [], Tokens) ->
    lists:reverse(Tokens);
tokens([], _, Token, Tokens) ->
    lists:reverse([lists:reverse(Token) | Tokens]);
tokens([C | Cs], Seps, Token, Tokens) ->
    case lists:member(C, Seps) of
        true when Token =:= [] -> tokens(Cs, Seps, [], Tokens);
        true -> tokens(Cs, Seps, [], [lists:reverse(Token) | Tokens]);
        false -> tokens(Cs, Seps, [C | Token], Tokens)
    end.

%% Removes whitespace (space, tab, newline, carriage return) at both ends.
trim(String) -> lists:reverse(trim_leading(lists:reverse(trim_leading(String)))).

trim_leading([C | Cs]) when C =:= $\s; C =:= $\t; C =:= $\n; C =:= $\r ->
    trim_leading(Cs);
trim_leading(String) ->
    String.

to_upper(String) -> [upper(C) || C <- String].
to_lower(String) -> [lower(C) || C <- String].
uppercase(String) -> to_upper(String).
lowercase(String) -> to_lower(String).

upper(C) when C >= $a, C =< $z -> C - 32;
upper(C) -> C.

lower(C) when C >= $A, C =< $Z -> C + 32;
lower(C) -> C.
