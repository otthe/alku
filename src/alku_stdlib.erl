%% @doc Alku's standard library: modules written in Alku itself, stored in
%% priv/stdlib and compiled into every program.
-module(alku_stdlib).

-export([modules/0, is_stdlib_module/1]).

-define(MODULES, [lists, maps, string]).

%% Parsed and checked forms of every standard library module.
-spec modules() -> [{module(), [erl_parse:abstract_form()]}].
modules() ->
    [{Module, load(Module)} || Module <- ?MODULES].

-spec is_stdlib_module(module()) -> boolean().
is_stdlib_module(Module) ->
    lists:member(Module, ?MODULES).

load(Module) ->
    File = atom_to_list(Module) ++ ".erl",
    {ok, Source} = alku_priv:read_file(["stdlib", File]),
    case alku_frontend:read_string(File, Source) of
        {ok, Forms} -> Forms;
        {error, Errors} -> error({broken_stdlib, File, Errors})
    end.
