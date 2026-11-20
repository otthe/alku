-module(guards).
-export([main/0]).

sign(N) when N < 0 -> negative;
sign(0) -> zero;
sign(_) -> positive.

describe(X) when is_integer(X), X rem 2 =:= 0 -> even;
describe(X) when is_integer(X) -> odd;
describe(X) when X =:= yes; X =:= no -> answer;
describe(_) -> 'something else'.

size_of(N) ->
    if
        N < 10 -> small;
        N < 100 -> medium;
        true -> large
    end.

main() ->
    show(sign(-5)), show(sign(0)), show(sign(7)),
    show(describe(4)), show(describe(7)), show(describe(no)), show(describe(other)),
    show(size_of(3)), show(size_of(42)), show(size_of(1000)),
    show(3 > 2 andalso 2 > 1),
    show(false orelse 1 =:= 2),
    show(not true),
    show(-7 div 2), show(-7 rem 2),
    show(abs(-12)).

show(Term) ->
    io:write(Term),
    io:nl().
