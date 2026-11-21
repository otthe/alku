-module(bitstrings).
-export([main/0]).

%% A bitstring's length need not be a whole number of bytes.
bits(<<>>) -> [];
bits(<<B:1, Rest/bitstring>>) -> [B | bits(Rest)].

main() ->
    Flags = <<1:1, 0:1, 1:1>>,
    io:format("~w ~p ~p~n", [Flags, bit_size(Flags), byte_size(Flags)]),
    io:format("~p~n", [bits(<<"A">>)]),
    io:format("~p ~p~n", [is_binary(Flags), is_bitstring(Flags)]),
    <<Head:4, Tail/bitstring>> = <<16#F0, 16#0F>>,
    io:format("~p ~w~n", [Head, Tail]),
    Packed = << <<X:3>> || X <- [1, 2, 3, 4] >>,
    io:format("~w~n", [Packed]),
    io:format("~p~n", [[X || <<X:3>> <= Packed]]),
    io:format("~w~n", [list_to_bitstring([<<1:1>>, 255, <<0:7>>])]),
    io:format("~w~n", [bitstring_to_list(<<1, 2, 3:2>>)]),
    U16 = <<"hé"/utf16>>,
    io:format("~w~n", [U16]),
    io:format("~w~n", [[C || <<C/utf16>> <= U16]]),
    io:format("~w~n", [<<16#1F600/utf32-little>>]),
    <<Smile/utf16>> = <<16#D83D:16, 16#DE00:16>>,
    io:format("~p~n", [Smile]).
