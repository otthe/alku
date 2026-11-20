%% @doc Compiles and runs every program in examples/ and compares what it
%% prints (or the compile errors) with the matching .expected file.
%%
%% To add a test, drop NAME.erl and NAME.expected into examples/.
%% Optional: NAME.input is given to the program as stdin, and NAME.args
%% holds its command-line arguments, one per line.
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
            Normal = run(Exe, Src, []),
            Stressed = run(Exe, Src, [{"ALKU_GC_STRESS", "1"}]),
            ok = file:delete(Exe),
            ?assertEqual(Expected, output(Warnings, Normal)),
            ?assertEqual(Expected, output(Warnings, Stressed));
        {error, Errors} ->
            ?assertEqual(Expected, output(Errors, <<>>))
    end.

%% Runs the program through sh, to give it NAME.input (or nothing) as stdin.
run(Exe, Src, Env) ->
    Base = filename:rootname(Src),
    Input = case filelib:is_regular(Base ++ ".input") of
                true -> Base ++ ".input";
                false -> "/dev/null"
            end,
    Args = case file:read_file(Base ++ ".args") of
               {ok, Text} -> string:lexemes(binary_to_list(Text), "\n");
               {error, _} -> []
           end,
    Sh = os:find_executable("sh"),
    Script = "exec \"$0\" \"$@\" < \"$ALKU_TEST_INPUT\"",
    {_Status, Output} = alku_os:run(Sh, ["-c", Script, Exe | Args],
                                    [{"ALKU_TEST_INPUT", Input} | Env]),
    Output.

%% Everything a user would see: compiler messages, then program output.
output(Messages, ProgramOutput) ->
    unicode:characters_to_binary([[[M, "\n"] || M <- Messages], ProgramOutput]).
