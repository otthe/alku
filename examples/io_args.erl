-module(io_args).
-export([main/1]).

%% main/1 gets the command-line arguments as a list of strings.
main(Args) ->
    io:format("args: ~p~n", [Args]),
    Numbers = [list_to_integer(A) || A <- Args],
    io:format("sum: ~p~n", [lists:sum(Numbers)]),
    Name = string:trim(io:get_line("name? ")),
    io:format("~nhello ~s~n", [Name]),
    File = "/tmp/alku_io_args_example.txt",
    ok = file:write_file(File, ["first line\n", "second line\n"]),
    {ok, Fd} = file:open(File, [read]),
    Lines = read_lines(Fd),
    ok = file:close(Fd),
    io:format("~p~n", [Lines]),
    io:format("opening a missing file gives ~p~n", [file:open("/nonexistent/dir/file", [read])]),
    io:put_chars("bye\n"),
    halt(3).

read_lines(Fd) ->
    case io:get_line(Fd, "") of
        eof -> [];
        Line -> [string:trim(Line) | read_lines(Fd)]
    end.
