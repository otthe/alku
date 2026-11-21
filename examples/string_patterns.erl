-module(string_patterns).
-export([main/0]).

%% "Prefix" ++ Rest matches a string that starts with Prefix.
route("GET " ++ Path) -> {get, Path};
route("POST " ++ Path) -> {post, Path};
route(_) -> unknown.

parse_header("Content-Length: " ++ Value) -> {length, list_to_integer(Value)};
parse_header("X-" ++ Name) -> {custom, Name};
parse_header(Other) -> {other, Other}.

main() ->
    io:format("~p~n", [route("GET /index.html")]),
    io:format("~p~n", [route("POST /login")]),
    io:format("~p~n", [route("DELETE /x")]),
    io:format("~p~n", [parse_header("Content-Length: 42")]),
    io:format("~p~n", [parse_header("X-Trace")]),
    io:format("~p~n", [parse_header("Host")]),
    "ab" ++ "c" ++ Rest = "abcdef",
    io:format("~s~n", [Rest]),
    case "hello world" of
        "hello " ++ Who -> io:format("hi ~s~n", [Who])
    end.
