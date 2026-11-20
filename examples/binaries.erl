-module(binaries).
-export([main/0]).

%% A tiny binary protocol: <<Type:8, Length:16, Payload:Length/binary>>
encode(Type, Payload) -> <<Type:8, (byte_size(Payload)):16, Payload/binary>>.

decode(<<>>) -> [];
decode(<<Type:8, Len:16, Payload:Len/binary, Rest/binary>>) -> [{Type, Payload} | decode(Rest)].

sum_bytes(<<X, Rest/binary>>, Acc) -> sum_bytes(Rest, Acc + X);
sum_bytes(<<>>, Acc) -> Acc.

main() ->
    B = <<1, 2, 300:16, -1:8/signed>>,
    io:format("~p ~p ~p~n", [B, byte_size(B), binary_to_list(B)]),
    <<A:4, C:4, _/binary>> = <<16#AB, 0>>,
    io:format("~p ~p~n", [A, C]),
    Msg = <<(encode(1, <<"hello">>))/binary, (encode(2, <<"world!">>))/binary>>,
    io:format("~p~n", [Msg]),
    io:format("~p~n", [decode(Msg)]),
    <<F:64/float, G:32/float-little>> = <<1.5:64/float, 2.0:32/float-little>>,
    io:format("~p ~p~n", [F, G]),
    <<N:16/little, S:8/signed>> = <<1, 2, 255>>,
    io:format("~p ~p~n", [N, S]),
    Big = 1 bsl 100,
    <<Back:128>> = <<Big:128>>,
    io:format("~p~n", [Back =:= Big]),
    Text = <<"hyvää päivää"/utf8>>,
    io:format("~p ~p~n", [byte_size(Text), length(unicode:characters_to_list(Text))]),
    io:format("~s~n", [Text]),
    io:format("~p~n", [[Ch || <<Ch/utf8>> <= <<"aä"/utf8>>]]),
    io:format("~p~n", [<< <<(X * 2)>> || <<X>> <= <<1, 2, 3>> >>]),
    io:format("~p~n", [binary:split(<<"a,b,,c">>, <<",">>, [global])]),
    io:format("~p~n", [binary:split(<<"key=value">>, <<"=">>)]),
    io:format("~p ~p~n", [list_to_binary(["ab", <<"cd">>, $e]), iolist_size(["ab", <<"cd">>])]),
    io:format("~p ~p~n", [atom_to_binary(ok), binary_to_integer(<<"-42">>)]),
    io:format("~p~n", [sum_bytes(list_to_binary(lists:duplicate(100000, 7)), 0)]),
    case <<"GET /index.html">> of
        <<"GET ", Path/binary>> -> io:format("path ~s~n", [Path])
    end,
    ok = file:write_file("/tmp/alku_binaries_example.bin", [Msg, <<"end">>]),
    {ok, Read} = file:read_file("/tmp/alku_binaries_example.bin"),
    io:format("~p~n", [Read =:= <<Msg/binary, "end">>]),
    io:format("~p ~p~n", [<<"abc">> < <<"abd">>, <<1, 2>> == <<1, 2>>]).
