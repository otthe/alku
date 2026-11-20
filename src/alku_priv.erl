%% @doc Reads files shipped in priv/ (the C runtime and the standard library).
%%
%% Normally priv/ is a directory on disk. Inside the alku escript it is
%% stored in the escript's zip archive instead (see rebar.config).
-module(alku_priv).

-export([read_file/1]).

-spec read_file([string()]) -> {ok, binary()} | error.
read_file(Parts) ->
    case code:priv_dir(alku) of
        Priv when is_list(Priv) ->
            case file:read_file(filename:join([Priv | Parts])) of
                {ok, Bin} -> {ok, Bin};
                {error, _} -> from_escript(Parts)
            end;
        {error, _} ->
            from_escript(Parts)
    end.

from_escript(Parts) ->
    InArchive = filename:join(["alku", "priv" | Parts]),
    case escript:extract(escript:script_name(), []) of
        {ok, Sections} ->
            case lists:keyfind(archive, 1, Sections) of
                {archive, Archive} ->
                    case zip:extract(Archive, [memory, {file_list, [InArchive]}]) of
                        {ok, [{_, Bin}]} -> {ok, Bin};
                        _ -> error
                    end;
                false ->
                    error
            end;
        {error, _} ->
            error
    end.
