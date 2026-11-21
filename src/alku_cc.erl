%% @doc Turns generated C into an executable with the system's C compiler.
%%
%% The compiler is taken from $ALKU_CC, or the first of cc, gcc and clang
%% found on the PATH. The C runtime ships in priv/runtime; it is copied to a
%% temporary directory for each build, because inside the alku escript it
%% lives in an archive that the C compiler cannot read.
-module(alku_cc).

-export([compile/2]).

-define(RUNTIME_HEADERS, ["alku_runtime.h", "alku_internal.h", "alku_gc.h"]).
-define(RUNTIME_SOURCES, ["alku_runtime.c", "alku_number.c", "alku_map.c",
                          "alku_binary.c", "alku_term.c", "alku_gc.c"]).

-spec compile(file:filename(), file:filename()) -> ok | {error, string()}.
compile(CFile, Exe) ->
    case find_compiler() of
        false ->
            {error, "no C compiler found: install gcc or clang, or set ALKU_CC"};
        CC ->
            RuntimeDir = alku_os:temp_file("alku_runtime"),
            Result = case write_runtime(RuntimeDir) of
                         ok -> run_compiler(CC, CFile, Exe, RuntimeDir);
                         {error, _} = Error -> Error
                     end,
            ok = file:del_dir_r(RuntimeDir),
            Result
    end.

run_compiler(CC, CFile, Exe, RuntimeDir) ->
    Sources = [filename:join(RuntimeDir, Name) || Name <- ?RUNTIME_SOURCES],
    Args = ["-std=c11", "-O2", "-pthread", "-I", RuntimeDir, "-o", Exe, CFile | Sources] ++ ["-lm"],
    case alku_os:run(CC, Args) of
        {0, _} ->
            ok;
        {Status, Output} ->
            {error, format("C compiler failed with exit status ~b:~n~ts", [Status, Output])}
    end.

write_runtime(Dir) ->
    ok = file:make_dir(Dir),
    write_runtime(Dir, ?RUNTIME_HEADERS ++ ?RUNTIME_SOURCES).

write_runtime(_Dir, []) ->
    ok;
write_runtime(Dir, [Name | Names]) ->
    case alku_priv:read_file(["runtime", Name]) of
        {ok, Bin} ->
            ok = file:write_file(filename:join(Dir, Name), Bin),
            write_runtime(Dir, Names);
        error ->
            {error, format("the Alku runtime file ~s is missing; rebuild with "
                           "`rebar3 escriptize`", [Name])}
    end.

find_compiler() ->
    Candidates = case os:getenv("ALKU_CC") of
                     false -> ["cc", "gcc", "clang"];
                     CC -> [CC]
                 end,
    first_executable(Candidates).

first_executable([]) ->
    false;
first_executable([Name | Rest]) ->
    case os:find_executable(Name) of
        false -> first_executable(Rest);
        Path -> Path
    end.

format(Format, Args) ->
    lists:flatten(io_lib:format(Format, Args)).
