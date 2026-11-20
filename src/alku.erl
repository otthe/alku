%% @doc The `alku` command-line tool.
-module(alku).

-export([main/1]).

-define(USAGE,
"usage:
  alku run FILE.erl              compile and run a program
  alku build FILE.erl [-o OUT]   compile a program into an executable
  alku c FILE.erl                print the generated C code
").

main(["run", Src]) -> run(Src);
main(["build", Src]) -> build(Src, filename:rootname(Src));
main(["build", Src, "-o", Exe]) -> build(Src, Exe);
main(["c", Src]) -> emit_c(Src);
main(_) -> io:put_chars(standard_error, ?USAGE), halt(2).

run(Src) ->
    Exe = alku_os:temp_file("alku_run"),
    compile_or_halt(Src, Exe),
    {Status, ok} = alku_os:run(Exe, [], fun(Data, ok) -> io:put_chars(Data) end, ok),
    _ = file:delete(Exe),
    halt(Status).

build(Src, Exe) ->
    compile_or_halt(Src, Exe),
    halt(0).

emit_c(Src) ->
    case alku_compiler:to_c(Src) of
        {ok, C, Warnings} ->
            report(Warnings),
            io:put_chars(C),
            halt(0);
        {error, Errors} ->
            report(Errors),
            halt(1)
    end.

compile_or_halt(Src, Exe) ->
    case alku_compiler:build(Src, Exe) of
        {ok, Warnings} -> report(Warnings);
        {error, Errors} -> report(Errors), halt(1)
    end.

report(Messages) ->
    lists:foreach(fun(M) -> io:format(standard_error, "~ts~n", [M]) end, Messages).
