-module(unsupported_receive).
-export([main/0]).

main() ->
    receive
        Msg -> Msg
    end.
