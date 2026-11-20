%% @doc Reads Alku source code and checks it.
%%
%% Alku uses Erlang syntax, so it reuses Erlang's own tools: epp (the
%% preprocessor and parser, which handles -define and macros), erl_lint
%% (which reports unbound variables, undefined functions and so on) and
%% erl_expand_records (which turns records into plain tuples).
-module(alku_frontend).

-export([read/1, read_string/2]).

%% Reads a source file. Returns its forms and any warnings.
-spec read(file:filename()) ->
    {ok, [erl_parse:abstract_form()], [string()]} | {error, [string()]}.
read(File) ->
    case epp:parse_file(File, []) of
        {ok, Forms} ->
            lint(File, Forms);
        {error, Reason} ->
            {error, [io_lib:format("~ts: ~ts", [File, file:format_error(Reason)])]}
    end.

%% Reads source code from a binary (no macros). Used for the standard
%% library, which must compile without errors or warnings.
-spec read_string(string(), binary()) ->
    {ok, [erl_parse:abstract_form()]} | {error, [string()]}.
read_string(Name, Source) ->
    case erl_scan:string(unicode:characters_to_list(Source), 1) of
        {ok, Tokens, _} ->
            case parse_forms(split_forms(Tokens, []), []) of
                {ok, Forms} ->
                    case lint(Name, Forms) of
                        {ok, Expanded, []} -> {ok, Expanded};
                        {ok, _, Warnings} -> {error, Warnings};
                        {error, _} = Error -> Error
                    end;
                {error, {Line, Mod, Desc}} ->
                    {error, [io_lib:format("~ts:~b: ~ts", [Name, Line, Mod:format_error(Desc)])]}
            end;
        {error, {Line, Mod, Desc}, _} ->
            {error, [io_lib:format("~ts:~b: ~ts", [Name, Line, Mod:format_error(Desc)])]}
    end.

split_forms([], []) -> [];
split_forms([], Acc) -> [lists:reverse(Acc)]; % missing final dot: reported by the parser
split_forms([{dot, _} = Dot | Rest], Acc) -> [lists:reverse([Dot | Acc]) | split_forms(Rest, [])];
split_forms([Token | Rest], Acc) -> split_forms(Rest, [Token | Acc]).

parse_forms([], Acc) ->
    {ok, lists:reverse(Acc)};
parse_forms([Tokens | Rest], Acc) ->
    case erl_parse:parse_form(Tokens) of
        {ok, Form} -> parse_forms(Rest, [Form | Acc]);
        {error, _} = Error -> Error
    end.

lint(File, Forms) ->
    case erl_lint:module(Forms, File) of
        {ok, Warnings} ->
            {ok, erl_expand_records:module(Forms, []), messages(Warnings, "Warning: ")};
        {error, Errors, Warnings} ->
            {error, messages(Errors, "") ++ messages(Warnings, "Warning: ")}
    end.

messages(Groups, Prefix) ->
    [io_lib:format("~ts:~s: ~s~ts", [F, location(Loc), Prefix, Mod:format_error(Desc)])
     || {F, Items} <- Groups, {Loc, Mod, Desc} <- Items].

location({Line, Column}) -> io_lib:format("~b:~b", [Line, Column]);
location(Line) when is_integer(Line) -> integer_to_list(Line);
location(_) -> "?".
