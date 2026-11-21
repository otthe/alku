-module(term_storage).
-export([main/0]).

%% term_to_binary/binary_to_term save any term to bytes and back, in the
%% same format Erlang uses.
main() ->
    Data = #{name => "Alku", version => {0, 7}, tags => [compiled, <<"erlang">>],
             big => 1 bsl 70},
    Bin = term_to_binary(Data),
    io:format("~p~n", [binary_to_term(Bin) =:= Data]),
    io:format("~w~n", [term_to_binary({ok, 1, "hi"})]),
    ok = file:write_file("/tmp/alku_term_storage.bin", Bin),
    {ok, Read} = file:read_file("/tmp/alku_term_storage.bin"),
    #{name := Name, version := {Major, Minor}} = binary_to_term(Read),
    io:format("~s ~b.~b~n", [Name, Major, Minor]),
    io:format("~p~n", [binary_to_term(<<131, 100, 0, 5, "hello">>)]),
    io:format("~p~n", [try binary_to_term(<<1, 2, 3>>) catch error:badarg -> bad end]),
    Ref = make_ref(),
    io:format("~p ~p~n", [is_reference(Ref), binary_to_term(term_to_binary(Ref)) =:= Ref]),
    io:format("~p ~p~n", [erlang:phash2(Data) =:= erlang:phash2(binary_to_term(Bin)),
                          erlang:phash2(Data, 10) < 10]),
    U1 = erlang:unique_integer([positive]),
    U2 = erlang:unique_integer([positive]),
    io:format("~p~n", [U2 > U1]),
    io:format("~p ~p ~p~n", [erlang:insert_element(2, {a, c}, b), erlang:delete_element(1, {a, b}),
                             erlang:append_element({a}, b)]),
    Sum = binary_to_term(term_to_binary(fun lists:sum/1)),
    io:format("~p ~p~n", [Sum, Sum([1, 2, 3])]).
