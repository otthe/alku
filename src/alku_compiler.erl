%% @doc The compiler pipeline:
%%
%%   source file -> alku_frontend (parse + check) -> alku_codegen (C code,
%%   together with the standard library) -> alku_cc (C compiler) -> executable
-module(alku_compiler).

-export([to_c/1, build/2]).

%% Translates a source file to C. Returns the C code and any warnings.
-spec to_c(file:filename()) -> {ok, iodata(), [string()]} | {error, [string()]}.
to_c(Src) ->
    case alku_frontend:read(Src) of
        {ok, Forms, Warnings} ->
            [Module | _] = [M || {attribute, _, module, M} <- Forms],
            case alku_stdlib:is_stdlib_module(Module) of
                true ->
                    {error, Warnings ++ [format("~ts: the module name ~s is used by "
                                                "Alku's standard library", [Src, Module])]};
                false ->
                    generate(Src, Module, Forms, Warnings)
            end;
        {error, Errors} ->
            {error, Errors}
    end.

generate(Src, Module, Forms, Warnings) ->
    case alku_codegen:program([{Module, Forms} | alku_stdlib:modules()]) of
        {ok, C} ->
            {ok, C, Warnings};
        {error, {ErrorModule, Line, Message}} ->
            File = case ErrorModule of
                       Module -> Src;
                       _ -> atom_to_list(ErrorModule) ++ ".erl (standard library)"
                   end,
            {error, Warnings ++ [format("~ts:~b: ~ts", [File, Line, Message])]}
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

format(Format, Args) ->
    lists:flatten(io_lib:format(Format, Args)).
