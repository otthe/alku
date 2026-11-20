%% @doc Turns generated C into an executable with the system's C compiler.
%%
%% The compiler is taken from $ALKU_CC, or the first of cc, gcc and clang
%% found on the PATH. The C runtime ships in priv/runtime; it is copied to a
%% temporary directory for each build, because inside the alku escript it
%% lives in an archive that the C compiler cannot read.
-module(alku_cc).

-export([compile/2]).

-define(RUNTIME_FILES, ["alku_runtime.h", "alku_runtime.c"]).

-spec compile(file:filename(), file:filename()) -> ok | {error, string()}.
compile(CFile, Exe) ->
    case find_compiler() of
        false ->
            {error, "no C compiler found: install gcc or clang, or set ALKU_CC"};
        CC ->
            RuntimeDir = alku_os:temp_file("alku_runtime"),
            ok = write_runtime(RuntimeDir),
            Args = ["-std=c11", "-O2", "-I", RuntimeDir, "-o", Exe, CFile,
                    filename:join(RuntimeDir, "alku_runtime.c")],
            Result = alku_os:run(CC, Args),
            ok = file:del_dir_r(RuntimeDir),
            case Result of
                {0, _} ->
                    ok;
                {Status, Output} ->
                    {error, lists:flatten(io_lib:format(
                              "C compiler failed with exit status ~b:~n~ts",
                              [Status, Output]))}
            end
    end.

%% erl_prim_loader reads both normal files and files inside the escript.
write_runtime(Dir) ->
    ok = file:make_dir(Dir),
    Source = filename:join(code:priv_dir(alku), "runtime"),
    lists:foreach(
      fun(Name) ->
              {ok, Bin, _} = erl_prim_loader:get_file(filename:join(Source, Name)),
              ok = file:write_file(filename:join(Dir, Name), Bin)
      end,
      ?RUNTIME_FILES).

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
