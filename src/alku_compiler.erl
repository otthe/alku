%% @doc The compiler pipeline:
%%
%%   source file -> alku_frontend (parse + check) -> alku_codegen (C code)
%%               -> alku_cc (C compiler) -> executable
-module(alku_compiler).

-export([to_c/1, build/2]).

%% Translates a source file to C. Returns the C code and any warnings.
-spec to_c(file:filename()) -> {ok, iodata(), [string()]} | {error, [string()]}.
to_c(Src) ->
    case alku_frontend:read(Src) of
        {ok, Forms, Warnings} ->
            case alku_codegen:module(Forms) of
                {ok, C} ->
                    {ok, C, Warnings};
                {error, {Line, Message}} ->
                    {error, Warnings ++ [io_lib:format("~ts:~b: ~ts", [Src, Line, Message])]}
            end;
        {error, Errors} ->
            {error, Errors}
    end.

%% Compiles a source file into the executable Exe.
-spec build(file:filename(), file:filename()) ->
    {ok, [string()]} | {error, [string()]}.
build(Src, Exe) ->
    case to_c(Src) of
        {ok, C, Warnings} ->
            CFile = alku_os:temp_file("alku") ++ ".c",
            ok = file:write_file(CFile, C),
            Result = alku_cc:compile(CFile, Exe),
            ok = file:delete(CFile),
            case Result of
                ok -> {ok, Warnings};
                {error, Message} -> {error, Warnings ++ [Message]}
            end;
        {error, Errors} ->
            {error, Errors}
    end.
