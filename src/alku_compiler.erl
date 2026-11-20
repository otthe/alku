%% @doc The compiler pipeline:
%%
%%   source files -> alku_frontend (parse + check) -> alku_codegen (C code,
%%   together with the standard library) -> alku_cc (C compiler) -> executable
%%
%% A program is its main module plus every module it uses: when a module
%% calls shapes:area(...), shapes.erl is loaded from the main module's
%% directory, and so on for the modules that one uses.
-module(alku_compiler).

-export([to_c/1, build/2]).

%% Modules provided by the C runtime; programs cannot define these.
-define(BUILTIN_MODULES, [erlang, io, file, math, binary, unicode]).

%% Translates a program to C. Returns the C code and any warnings.
-spec to_c(file:filename()) -> {ok, iodata(), [string()]} | {error, [string()]}.
to_c(Src) ->
    case load([Src], filename:dirname(Src), [], #{}, []) of
        {ok, Modules, Files, Warnings} ->
            case alku_codegen:program(Modules ++ alku_stdlib:modules()) of
                {ok, C} ->
                    {ok, C, Warnings};
                {error, {Module, Line, Message}} ->
                    File = maps:get(Module, Files,
                                    atom_to_list(Module) ++ ".erl (standard library)"),
                    {error, Warnings ++ [format("~ts:~b: ~ts", [File, Line, Message])]}
            end;
        {error, Errors} ->
            {error, Errors}
    end.

%% Compiles a program into the executable Exe.
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

%% Reads the files in Queue, adding the modules they use that have a
%% source file in Dir. The main module comes first in the result.
load([], _Dir, Modules, Files, Warnings) ->
    {ok, lists:reverse(Modules), Files, Warnings};
load([File | Queue], Dir, Modules, Files, Warnings) ->
    case alku_frontend:read(File) of
        {ok, Forms, NewWarnings} ->
            [Module | _] = [M || {attribute, _, module, M} <- Forms],
            case check_module(Module, File, Modules =:= [], Files) of
                ok ->
                    Known = Files#{Module => File},
                    Used = [source(Dir, M) || M <- used_modules(Forms),
                                              not maps:is_key(M, Known), not reserved(M)],
                    New = [F || F <- lists:usort(Used), filelib:is_regular(F),
                                not lists:member(F, Queue)],
                    load(Queue ++ New, Dir, [{Module, Forms} | Modules], Known,
                         Warnings ++ NewWarnings);
                {error, Message} ->
                    {error, Warnings ++ NewWarnings ++ [Message]}
            end;
        {error, Errors} ->
            {error, Warnings ++ Errors}
    end.

check_module(Module, File, IsMain, Files) ->
    Expected = filename:basename(File, ".erl"),
    WrongName = not IsMain andalso Expected =/= atom_to_list(Module),
    if
        WrongName ->
            {error, format("~ts: defines module ~s, but the file name says ~s",
                           [File, Module, Expected])};
        is_map_key(Module, Files) ->
            {error, format("~ts: module ~s is already defined in ~ts",
                           [File, Module, maps:get(Module, Files)])};
        true ->
            case reserved(Module) of
                true -> {error, format("~ts: the module name ~s is reserved by Alku",
                                       [File, Module])};
                false -> ok
            end
    end.

reserved(Module) ->
    lists:member(Module, ?BUILTIN_MODULES) orelse alku_stdlib:is_stdlib_module(Module).

source(Dir, Module) ->
    filename:join(Dir, atom_to_list(Module) ++ ".erl").

%% Modules named in remote calls, fun M:F/A and -import.
used_modules(Forms) ->
    lists:usort(modules_in(Forms)).

modules_in({remote, _, {atom, _, M}, F}) -> [M | modules_in(F)];
modules_in({'fun', _, {function, {atom, _, M}, _, _}}) -> [M];
modules_in({attribute, _, import, {M, _}}) -> [M];
modules_in(T) when is_tuple(T) -> modules_in(tuple_to_list(T));
modules_in(L) when is_list(L) -> lists:flatmap(fun modules_in/1, L);
modules_in(_) -> [].

format(Format, Args) ->
    lists:flatten(io_lib:format(Format, Args)).
