%% @doc Reads an Alku source file and checks it.
%%
%% Alku uses Erlang syntax, so it reuses Erlang's own tools: epp (the
%% preprocessor and parser, which handles -define and macros) and erl_lint
%% (which reports unbound variables, undefined functions and so on).
-module(alku_frontend).

-export([read/1]).

-spec read(file:filename()) ->
    {ok, [erl_parse:abstract_form()], [string()]} | {error, [string()]}.
read(File) ->
    case epp:parse_file(File, []) of
        {ok, Forms} ->
            lint(File, Forms);
        {error, Reason} ->
            {error, [io_lib:format("~ts: ~ts", [File, file:format_error(Reason)])]}
    end.

lint(File, Forms) ->
    case erl_lint:module(Forms, File) of
        {ok, Warnings} ->
            {ok, Forms, messages(Warnings, "Warning: ")};
        {error, Errors, Warnings} ->
            {error, messages(Errors, "") ++ messages(Warnings, "Warning: ")}
    end.

messages(Groups, Prefix) ->
    [io_lib:format("~ts:~s: ~s~ts", [F, location(Loc), Prefix, Mod:format_error(Desc)])
     || {F, Items} <- Groups, {Loc, Mod, Desc} <- Items].

location({Line, Column}) -> io_lib:format("~b:~b", [Line, Column]);
location(Line) when is_integer(Line) -> integer_to_list(Line);
location(_) -> "?".
