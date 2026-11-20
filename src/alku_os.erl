%% @doc Running external programs and creating temporary file names.
-module(alku_os).

-export([run/2, run/3, run/5, temp_file/1]).

-type env() :: [{string(), string()}].

%% Runs Exe (an absolute path) and returns its exit status and all output
%% (stdout and stderr combined).
-spec run(file:filename(), [string()]) -> {non_neg_integer(), binary()}.
run(Exe, Args) ->
    run(Exe, Args, []).

%% Like run/2, with extra environment variables.
-spec run(file:filename(), [string()], env()) -> {non_neg_integer(), binary()}.
run(Exe, Args, Env) ->
    {Status, Chunks} = run(Exe, Args, Env, fun(Data, Acc) -> [Acc, Data] end, []),
    {Status, iolist_to_binary(Chunks)}.

%% Like run/3, but folds Fun over the output as it arrives.
-spec run(file:filename(), [string()], env(), fun((binary(), Acc) -> Acc), Acc) ->
    {non_neg_integer(), Acc}.
run(Exe, Args, Env, Fun, Acc0) ->
    Port = open_port({spawn_executable, Exe},
                     [{args, Args}, {env, Env}, binary, exit_status,
                      stderr_to_stdout, use_stdio]),
    collect(Port, Fun, Acc0).

collect(Port, Fun, Acc) ->
    receive
        {Port, {data, Data}} -> collect(Port, Fun, Fun(Data, Acc));
        {Port, {exit_status, Status}} -> {Status, Acc}
    end.

%% A unique path in the system's temporary directory.
-spec temp_file(string()) -> file:filename().
temp_file(Prefix) ->
    Dir = case os:getenv("TMPDIR") of
              false -> "/tmp";
              "" -> "/tmp";
              D -> D
          end,
    Unique = os:getpid() ++ "_" ++ integer_to_list(erlang:unique_integer([positive])),
    filename:join(Dir, Prefix ++ "_" ++ Unique).
