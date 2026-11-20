%% @doc Compiles and runs every program in examples/ and compares what it
%% prints (or the compile errors) with the matching .expected file.
%%
%% To add a test, drop NAME.erl and NAME.expected into examples/.
%% Paths are relative to the project root, where rebar3 eunit runs.
-module(alku_examples_tests).

-include_lib("eunit/include/eunit.hrl").

-define(EXAMPLES, "examples").

examples_test_() ->
    Sources = filelib:wildcard(filename:join(?EXAMPLES, "*.erl")),
    [{Src, fun() -> check(Src) end} || Src <- Sources].

check(Src) ->
    {ok, Expected} = file:read_file(filename:rootname(Src) ++ ".expected"),
    ?assertEqual(Expected, output(Src)).

%% Everything a user would see: warnings, then program output, or the errors.
output(Src) ->
    Exe = alku_os:temp_file("alku_test"),
    case alku_compiler:build(Src, Exe) of
        {ok, Warnings} ->
            {_Status, Output} = alku_os:run(Exe, []),
            ok = file:delete(Exe),
            unicode:characters_to_binary([lines(Warnings), Output]);
        {error, Errors} ->
            unicode:characters_to_binary(lines(Errors))
    end.

lines(Messages) ->
    [[M, "\n"] || M <- Messages].
