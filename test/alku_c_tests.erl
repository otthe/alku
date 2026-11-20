-module(alku_c_tests).

-include_lib("eunit/include/eunit.hrl").

function_name_test() ->
    ?assertEqual("alku_fact_1", alku_c:function_name(fact, 1)),
    ?assertEqual("alku_is_even_1", alku_c:function_name(is_even, 1)),
    ?assertEqual("alku_main_0", alku_c:function_name(main, 0)).

function_name_escapes_unusual_atoms_test() ->
    ?assertEqual("alkuq_614062_2", alku_c:function_name('a@b', 2)).

var_name_test() ->
    ?assertEqual("v3_N", alku_c:var_name('N', 3)),
    ?assertEqual("v0_X_Y", alku_c:var_name('X@Y', 0)).

string_literal_test() ->
    ?assertEqual(<<"\"say \\\"hi\\\"\\012\"">>,
                 iolist_to_binary(alku_c:string_literal(<<"say \"hi\"\n">>))),
    ?assertEqual(<<"\"\\303\\244\"">>,  % "ä" as UTF-8 bytes
                 iolist_to_binary(alku_c:string_literal(<<"ä"/utf8>>))).

int64_literal_test() ->
    ?assertEqual(<<"INT64_C(42)">>, iolist_to_binary(alku_c:int64_literal(42))),
    ?assertEqual(<<"(-INT64_C(7))">>, iolist_to_binary(alku_c:int64_literal(-7))),
    ?assertEqual(<<"INT64_MIN">>,
                 iolist_to_binary(alku_c:int64_literal(-9223372036854775808))).

fits_int64_test() ->
    ?assert(alku_c:fits_int64(9223372036854775807)),
    ?assertNot(alku_c:fits_int64(9223372036854775808)).

indent_test() ->
    ?assertEqual(<<"f() {\n    if (x) {\n        y;\n    }\n}\n">>,
                 iolist_to_binary(alku_c:indent("f() {\nif (x) {\ny;\n}\n}\n"))).
