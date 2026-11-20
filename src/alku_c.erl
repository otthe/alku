%% @doc Small helpers for writing C source text: identifiers, literals and
%% indentation. Kept separate from code generation so they are easy to test.
-module(alku_c).

-export([function_name/2, var_name/2, string_literal/1, int64_literal/1,
         fits_int64/1, indent/1]).

-define(INT64_MIN, -9223372036854775808).
-define(INT64_MAX, 9223372036854775807).

%% C name of the Alku function Name/Arity, e.g. fact/1 -> "alku_fact_1".
%% Names with characters C does not allow are hex-encoded under a different
%% prefix, so two different functions can never get the same C name.
-spec function_name(atom(), non_neg_integer()) -> string().
function_name(Name, Arity) ->
    String = atom_to_list(Name),
    Stem = case lists:all(fun is_ident_char/1, String) of
               true -> "alku_" ++ String;
               false -> "alkuq_" ++ hex(String)
           end,
    Stem ++ "_" ++ integer_to_list(Arity).

%% C name for an Erlang variable. The counter N makes it unique, so
%% unusual characters can simply be replaced.
-spec var_name(atom(), non_neg_integer()) -> string().
var_name(Name, N) ->
    Safe = [case is_ident_char(C) of true -> C; false -> $_ end
            || C <- atom_to_list(Name)],
    "v" ++ integer_to_list(N) ++ "_" ++ Safe.

%% A C string literal for UTF-8 bytes. Anything outside printable ASCII is
%% written as an octal escape.
-spec string_literal(binary()) -> iolist().
string_literal(Bytes) ->
    [$", [escape(B) || <<B>> <= Bytes], $"].

-spec int64_literal(integer()) -> iolist().
int64_literal(?INT64_MIN) -> "INT64_MIN";
int64_literal(N) when N < 0 -> ["(-INT64_C(", integer_to_list(-N), "))"];
int64_literal(N) -> ["INT64_C(", integer_to_list(N), ")"].

-spec fits_int64(integer()) -> boolean().
fits_int64(N) -> N >= ?INT64_MIN andalso N =< ?INT64_MAX.

%% Indents generated C by brace depth so it is pleasant to read.
-spec indent(iodata()) -> iodata().
indent(Code) ->
    Lines = binary:split(iolist_to_binary(Code), <<"\n">>, [global]),
    {Indented, _Depth} = lists:mapfoldl(fun indent_line/2, 0, Lines),
    lists:join("\n", Indented).

%% Internal

indent_line(<<>>, Depth) ->
    {<<>>, Depth};
indent_line(Line, Depth0) ->
    Depth = case Line of
                <<"}", _/binary>> -> Depth0 - 1;
                _ -> Depth0
            end,
    Next = case binary:last(Line) of
               ${ -> Depth + 1;
               _ -> Depth
           end,
    {[lists:duplicate(4 * Depth, $\s), Line], Next}.

is_ident_char(C) ->
    (C >= $a andalso C =< $z) orelse (C >= $A andalso C =< $Z)
        orelse (C >= $0 andalso C =< $9) orelse C =:= $_.

hex(String) ->
    lists:flatten([io_lib:format("~2.16.0b", [B])
                   || <<B>> <= unicode:characters_to_binary(String)]).

escape($") -> "\\\"";
escape($\\) -> "\\\\";
escape($?) -> "\\?"; % avoids accidental C trigraphs like ??=
escape(B) when B >= 32, B < 127 -> B;
escape(B) -> io_lib:format("\\~3.8.0b", [B]).
