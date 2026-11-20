-module(case_exports).
-export([main/0]).

%% Variables bound in every clause of a case or if can be used after it.
describe(N) ->
    case N rem 2 of
        0 -> Kind = even;
        1 -> Kind = odd
    end,
    if
        N > 10 -> Size = big;
        true -> Size = small
    end,
    {Kind, Size}.

main() ->
    io:format("~p ~p~n", [describe(4), describe(13)]),
    case lists:member(x, [x]) of
        true -> Msg = "found", Count = 1;
        false -> Msg = "missing", Count = 0
    end,
    io:format("~s ~p~n", [Msg, Count]).
