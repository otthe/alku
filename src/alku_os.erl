%% @doc Running external programs and creating temporary file names.
-module(alku_os).

-export([run/2, run/4, temp_file/1]).

%% Runs Exe (an absolute path) and returns its exit status and all output
%% (stdout and stderr combined).
-spec run(file:filename(), [string()]) -> {non_neg_integer(), binary()}.
run(Exe, Args) ->
    {Status, Chunks} = run(Exe, Args, fun(Data, Acc) -> [Acc, Data] end, []),
    {Status, iolist_to_binary(Chunks)}.

%% Like run/2, but folds Fun over the output as it arrives.
-spec run(file:filename(), [string()], fun((binary(), Acc) -> Acc), Acc) ->
    {non_neg_integer(), Acc}.
run(Exe, Args, Fun, Acc0) ->
    Port = open_port({spawn_executable, Exe},
                     [{args, Args}, binary, exit_status, stderr_to_stdout, use_stdio]),
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
