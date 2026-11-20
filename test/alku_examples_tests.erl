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
    [{Src, {timeout, 120, fun() -> check(Src) end}} || Src <- Sources].

%% Each program runs twice: normally, and with the garbage collector
%% running very often (ALKU_GC_STRESS) to shake out memory bugs.
check(Src) ->
    {ok, Expected} = file:read_file(filename:rootname(Src) ++ ".expected"),
    Exe = alku_os:temp_file("alku_test"),
    case alku_compiler:build(Src, Exe) of
        {ok, Warnings} ->
            {_, Normal} = alku_os:run(Exe, []),
            {_, Stressed} = alku_os:run(Exe, [], [{"ALKU_GC_STRESS", "1"}]),
            ok = file:delete(Exe),
            ?assertEqual(Expected, output(Warnings, Normal)),
            ?assertEqual(Expected, output(Warnings, Stressed));
        {error, Errors} ->
            ?assertEqual(Expected, output(Errors, <<>>))
    end.

%% Everything a user would see: compiler messages, then program output.
output(Messages, ProgramOutput) ->
    unicode:characters_to_binary([[[M, "\n"] || M <- Messages], ProgramOutput]).
