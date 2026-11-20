-module(patterns).
-export([main/0]).

same(X, X) -> true;
same(_, _) -> false.

main() ->
    show(same(1, 1)), show(same(1, 2)), show(same(a, a)),
    A = 5,
    5 = A,
    B = C = A + 1,
    show(B + C),
    show(case A of 5 -> five; _ -> other end).

show(Term) ->
    io:write(Term),
    io:nl().
