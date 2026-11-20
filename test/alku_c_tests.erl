-module(alku_c_tests).

-include_lib("eunit/include/eunit.hrl").

function_name_test() ->
    ?assertEqual("alku_math__fact_1", alku_c:function_name(math, fact, 1)),
    ?assertEqual("alku_my_mod__is_even_1", alku_c:function_name(my_mod, is_even, 1)),
    ?assertEqual("alku_m__main_0", alku_c:function_name(m, main, 0)).

function_name_escapes_unusual_atoms_test() ->
    ?assertEqual("alku_m__q614062_2", alku_c:function_name(m, 'a@b', 2)),
    ?assertEqual("alku_q615f5f62__f_0", alku_c:function_name(a__b, f, 0)).

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
