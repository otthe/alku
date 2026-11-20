-module(records).
-export([main/0]).

-record(person, {name, age = 0, email = none}).

birthday(#person{age = Age} = P) -> P#person{age = Age + 1}.

is_adult(#person{age = Age}) when Age >= 18 -> true;
is_adult(#person{}) -> false.

main() ->
    P = #person{name = "Ada", age = 36},
    io:format("~p~n", [P]),
    io:format("~s is ~p~n", [P#person.name, P#person.age]),
    P2 = birthday(P),
    io:format("~p ~p~n", [P2#person.age, is_adult(P2)]),
    Kid = #person{name = "Bo"},
    io:format("~p ~p~n", [Kid, is_adult(Kid)]),
    io:format("~p~n", [[N || #person{name = N} <- [P, Kid]]]),
    io:format("~p ~p ~p~n", [record_info(fields, person), is_record(P, person), #person.age]).
