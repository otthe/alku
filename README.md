# Alku

Alku is a small compiled language with the syntax of sequential Erlang.
The compiler is written in Erlang, turns Alku programs into C, and lets
gcc or clang build a native executable.

```erlang
-module(factorial).
-export([main/0]).

fact(0) -> 1;
fact(N) when N > 0 -> N * fact(N - 1).

main() ->
    io:write(fact(20)),
    io:nl().
```

```
$ alku run factorial.erl
2432902008176640000
```

## Requirements

- Erlang/OTP 26 or newer and [rebar3](https://rebar3.org)
- A C compiler: `cc`, `gcc` or `clang` (or set `ALKU_CC`)

## Usage

```
rebar3 escriptize                 # builds _build/default/bin/alku
rebar3 eunit                      # runs all tests
rebar3 check                      # tests + dialyzer type checks

alku run FILE.erl                 # compile and run
alku build FILE.erl [-o OUT]      # build an executable (default: FILE)
alku c FILE.erl                   # show the generated C code
```

`_build/default/bin/alku` is a single file with the C runtime inside, so
you can put it on your PATH, e.g.
`ln -s "$PWD/_build/default/bin/alku" ~/.local/bin/alku`.

## What works now (phase 3)

- Integers (64-bit; overflow is a runtime error), atoms, tuples, lists,
  strings (lists of character codes, as in Erlang) and funs
- Functions with multiple clauses, pattern matching and guards (`,` and `;`)
- Patterns: variables, literals, `{...}`, `[H | T]`, `"abc"`, `P1 = P2`,
  repeated variables
- `case`, `if`, `begin ... end`, `=` matches
- Funs: `fun(X) -> ... end` with captured variables, named funs
  (`fun Loop(N) -> ... Loop(N - 1) end`), `fun f/1`, `fun lists:map/2`,
  `fun erlang:abs/1`
- **Proper tail calls**: any call in tail position (to the same function,
  another function, or a fun; inside `case`, `if`, `andalso`, `orelse`)
  runs in constant stack space. Non-tail recursion can go very deep too:
  programs run on a 512 MB stack.
- **Garbage collection**: memory that is no longer used is reclaimed
- Operators: `+ - * div rem`, comparisons (full Erlang term order),
  `and or xor not andalso orelse`, `++ --`
- Built-ins: `is_integer/1`, `is_atom/1`, `is_boolean/1`, `is_list/1`,
  `is_tuple/1`, `is_function/1,2`, `abs/1`, `hd/1`, `tl/1`, `length/1`,
  `element/2`, `setelement/3`, `tuple_size/1`, `integer_to_list/1`,
  `atom_to_list/1`, `io:write/1`, `io:nl/0`, `io:format/1,2`
- Standard library `lists`: `all any append duplicate filter flatten foldl
  foldr foreach last map max member min nth reverse seq sort sum zip`
- `io:format` controls: `~w ~p ~s ~c ~b ~B ~n ~~` (no field widths)
- Erlang-style runtime errors (`no function clause matching f(5)`, ...)
- Compile errors and warnings from Erlang's own linter

Programs that use only these features are also valid Erlang and print
the same output under `erl`. One difference: `~p` never breaks long
terms over several lines.

Not yet: floats, maps, binaries, big integers, exceptions (`try`),
list comprehensions, `"abc" ++ Rest` patterns, using a variable after
the `case` that bound it, programs made of several modules.

## How the runtime works

- **Values** are small C structs passed by value: a tag plus an integer,
  an atom name or a pointer to a heap object (tuple, list cell, fun).
- **Calls**: every Alku function and fun body is a C function taking its
  arguments as an array. A call in tail position to the function itself
  becomes a `goto` back to its start. Any other tail call returns a
  "pending call" to the nearest non-tail call site, which runs it
  (a trampoline), so the C stack never grows.
- **Garbage collector** (`alku_gc.c`): mark-and-sweep. Roots are found by
  scanning the C stack conservatively; heap objects are traced precisely.
  Set `ALKU_GC_STRESS=1` to collect very often, which the tests do to
  catch memory bugs.

## How it is organised

| Path | What it does |
|---|---|
| `src/alku.erl` | Command-line tool |
| `src/alku_compiler.erl` | The pipeline: frontend → codegen → C compiler |
| `src/alku_frontend.erl` | Parses with `epp` and checks with `erl_lint` |
| `src/alku_codegen.erl` | Turns Erlang abstract syntax into C (tail calls, funs) |
| `src/alku_c.erl` | Helpers for C names, literals and indentation |
| `src/alku_cc.erl` | Finds and runs the C compiler |
| `src/alku_os.erl` | Runs programs, makes temporary file names |
| `src/alku_priv.erl` | Reads files from `priv/` (also inside the escript) |
| `src/alku_stdlib.erl` | Loads the standard library modules |
| `priv/runtime/` | The C runtime: values, operators, calls, errors, GC |
| `priv/stdlib/` | The standard library, written in Alku |
| `test/` | Unit tests |
| `examples/` | Example programs with their expected output (also tests) |

## Tests

`rebar3 eunit` runs the unit tests and every program in `examples/`,
once normally and once with `ALKU_GC_STRESS=1`.
Each `NAME.erl` there has a `NAME.expected` file with exactly what the
program should print (or the compile errors it should produce). To add a
test, add a new pair of files.

## License

Alku uses Erlang/OTP's `epp` and `erl_lint` modules at compile time.
Erlang/OTP is licensed under the Apache License 2.0.
