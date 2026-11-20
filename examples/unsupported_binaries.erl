-module(unsupported_binaries).
-export([main/0]).

main() ->
    io:write(<<"hello">>),
    io:nl().
