%% The string module of Alku's standard library, written in Alku.
%%
%% A string is a list of characters or a UTF-8 binary; functions return
%% the same kind they are given. Case conversion covers ASCII and Latin-1.
-module(string).

-compile({no_auto_import, [length/1]}).

-export([chomp/1, equal/2, find/2, is_empty/1, join/2, length/1, lexemes/2,
         lowercase/1, pad/2, pad/3, prefix/2, replace/3, replace/4, reverse/1,
         slice/2, slice/3, split/2, split/3, to_integer/1, to_lower/1, to_upper/1,
         tokens/2, trim/1, trim/2, trim/3, uppercase/1]).

%% ---------- Helpers: work on character lists, give back the input's kind

chars(S) -> unicode:characters_to_list(S).

like(Original, Chars) when is_binary(Original) -> unicode:characters_to_binary(Chars);
like(_, Chars) -> Chars.

strip_prefix([], Rest) -> {ok, Rest};
strip_prefix([C | Prefix], [C | Rest]) -> strip_prefix(Prefix, Rest);
strip_prefix(_, _) -> nomatch.

%% ---------- Exported functions

%% Removes trailing newlines ("\n" and "\r\n").
chomp(S) -> like(S, lists:reverse(drop_newlines(lists:reverse(chars(S))))).

drop_newlines([$\n, $\r | Rest]) -> drop_newlines(Rest);
drop_newlines([$\n | Rest]) -> drop_newlines(Rest);
drop_newlines(Rest) -> Rest.

equal(A, B) -> chars(A) =:= chars(B).

%% The rest of S from the first Pattern on, or nomatch.
find(S, Pattern) ->
    case find_chars(chars(S), chars(Pattern)) of
        nomatch -> nomatch;
        Rest -> like(S, Rest)
    end.

find_chars(_, []) -> nomatch;
find_chars([], _) -> nomatch;
find_chars([_ | Cs] = S, Pattern) ->
    case strip_prefix(Pattern, S) of
        {ok, _} -> S;
        nomatch -> find_chars(Cs, Pattern)
    end.

is_empty(S) -> chars(S) =:= [].

%% join(["a", "b"], ", ") is "a, b"
join([], _) -> [];
join([X], _) -> X;
join([X | Xs], Sep) -> X ++ Sep ++ join(Xs, Sep).

length(S) -> erlang:length(chars(S)).

%% The parts between separator characters, without empty ones:
%% lexemes("a  b,c", " ,") is ["a", "b", "c"].
lexemes(S, Separators) -> [like(S, T) || T <- tokens_of(chars(S), chars(Separators), [], [])].

tokens(S, Separators) -> tokens_of(chars(S), chars(Separators), [], []).

tokens_of([], _, [], Tokens) ->
    lists:reverse(Tokens);
tokens_of([], _, Token, Tokens) ->
    lists:reverse([lists:reverse(Token) | Tokens]);
tokens_of([C | Cs], Seps, Token, Tokens) ->
    case lists:member(C, Seps) of
        true when Token =:= [] -> tokens_of(Cs, Seps, [], Tokens);
        true -> tokens_of(Cs, Seps, [], [lists:reverse(Token) | Tokens]);
        false -> tokens_of(Cs, Seps, [C | Token], Tokens)
    end.

to_upper(C) when is_integer(C) -> upper(C);
to_upper(S) -> like(S, [upper(C) || C <- chars(S)]).

to_lower(C) when is_integer(C) -> lower(C);
to_lower(S) -> like(S, [lower(C) || C <- chars(S)]).

uppercase(S) -> to_upper(S).
lowercase(S) -> to_lower(S).

upper(C) when C >= $a, C =< $z -> C - 32;
upper(C) when C >= 224, C =< 254, C =/= 247 -> C - 32; % à..þ
upper(C) -> C.

lower(C) when C >= $A, C =< $Z -> C + 32;
lower(C) when C >= 192, C =< 222, C =/= 215 -> C + 32; % À..Þ
lower(C) -> C.

%% Pads with spaces to Length characters: pad("ab", 4) is "ab  ".
pad(S, Length) -> pad(S, Length, trailing).

pad(S, Length, Direction) ->
    Chars = chars(S),
    Missing = erlang:max(0, Length - erlang:length(Chars)),
    Padded = case Direction of
                 trailing -> Chars ++ lists:duplicate(Missing, $\s);
                 leading -> lists:duplicate(Missing, $\s) ++ Chars;
                 both -> lists:duplicate(Missing div 2, $\s) ++ Chars ++
                             lists:duplicate(Missing - Missing div 2, $\s)
             end,
    like(S, Padded).

%% The rest of S after Prefix, or nomatch.
prefix(S, Prefix) ->
    case strip_prefix(chars(Prefix), chars(S)) of
        {ok, Rest} -> like(S, Rest);
        nomatch -> nomatch
    end.

%% Replaces the first (leading), last (trailing) or every (all) Pattern.
replace(S, Pattern, Replacement) -> replace(S, Pattern, Replacement, leading).

replace(S, Pattern, Replacement, Where) ->
    Parts = split_chars(chars(S), chars(Pattern), Where),
    like(S, join(Parts, chars(Replacement))).

reverse(S) -> like(S, lists:reverse(chars(S))).

%% Characters from Start (the first is 0), Length of them or to the end.
slice(S, Start) -> slice(S, Start, infinity).

slice(S, Start, Length) ->
    Rest = drop(Start, chars(S)),
    like(S, case Length of
                infinity -> Rest;
                _ -> lists:sublist(Rest, Length)
            end).

drop(N, [_ | Cs]) when N > 0 -> drop(N - 1, Cs);
drop(_, Cs) -> Cs.

%% Splits at the first Sep: split("a,b,c", ",") is ["a", "b,c"].
split(S, Sep) -> split(S, Sep, leading).

%% Where is leading, trailing (the last Sep) or all.
split(S, Sep, Where) -> [like(S, Part) || Part <- split_chars(chars(S), chars(Sep), Where)].

split_chars(S, [], _) -> [S];
split_chars(S, Sep, leading) -> split_first(S, Sep, []);
split_chars(S, Sep, trailing) ->
    Reversed = split_first(lists:reverse(S), lists:reverse(Sep), []),
    lists:reverse([lists:reverse(Part) || Part <- Reversed]);
split_chars(S, Sep, all) -> split_all(S, Sep, [], []).

split_first(S, Sep, Before) ->
    case strip_prefix(Sep, S) of
        {ok, After} -> [lists:reverse(Before), After];
        nomatch when S =:= [] -> [lists:reverse(Before)];
        nomatch -> split_first(tl(S), Sep, [hd(S) | Before])
    end.

split_all([], _, Current, Parts) ->
    lists:reverse([lists:reverse(Current) | Parts]);
split_all([C | Cs] = S, Sep, Current, Parts) ->
    case strip_prefix(Sep, S) of
        {ok, After} -> split_all(After, Sep, [], [lists:reverse(Current) | Parts]);
        nomatch -> split_all(Cs, Sep, [C | Current], Parts)
    end.

%% to_integer("42 apples") is {42, " apples"}; {error, no_integer} if
%% S does not start with digits.
to_integer(S) ->
    Chars = chars(S),
    {Sign, Rest} = case Chars of
                       [$- | R] -> {"-", R};
                       [$+ | R] -> {"", R};
                       _ -> {"", Chars}
                   end,
    case lists:splitwith(fun(C) -> C >= $0 andalso C =< $9 end, Rest) of
        {[], _} -> {error, no_integer};
        {Digits, After} -> {list_to_integer(Sign ++ Digits), like(S, After)}
    end.

%% Removes whitespace at both ends, the start (leading) or the end (trailing).
trim(S) -> trim(S, both).

trim(S, Direction) -> trim(S, Direction, [$\s, $\t, $\n, $\r, $\v, $\f]).

trim(S, Direction, Characters) ->
    Chars = chars(S),
    Trimmed = case Direction of
                  leading -> drop_any(Chars, Characters);
                  trailing -> lists:reverse(drop_any(lists:reverse(Chars), Characters));
                  both -> lists:reverse(drop_any(lists:reverse(drop_any(Chars, Characters)),
                                                 Characters))
              end,
    like(S, Trimmed).

drop_any([C | Cs] = S, Characters) ->
    case lists:member(C, Characters) of
        true -> drop_any(Cs, Characters);
        false -> S
    end;
drop_any([], _) -> [].
