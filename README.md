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

## What works now (phase 1)

- Integers (64-bit; overflow is a runtime error) and atoms
- Functions with multiple clauses, pattern matching and guards (`,` and `;`)
- `case`, `if`, `begin ... end`, `=` matches, repeated variables in patterns
- Operators: `+ - * div rem`, comparisons, `and or xor not andalso orelse`
- Built-ins: `is_integer/1`, `is_atom/1`, `is_boolean/1`, `abs/1`,
  `io:write/1`, `io:nl/0`
- Erlang-style runtime errors (`no function clause matching f(5)`, ...)
- Compile errors and warnings from Erlang's own linter

Programs that use only these features are also valid Erlang, so they
give the same output under `erl`.

Not yet: lists, tuples, strings, floats, funs, big integers, using a
variable after the `case` that bound it.

## How it is organised

| Path | What it does |
|---|---|
| `src/alku.erl` | Command-line tool |
| `src/alku_compiler.erl` | The pipeline: frontend → codegen → C compiler |
| `src/alku_frontend.erl` | Parses with `epp` and checks with `erl_lint` |
| `src/alku_codegen.erl` | Turns Erlang abstract syntax into C |
| `src/alku_c.erl` | Helpers for C names, literals and indentation |
| `src/alku_cc.erl` | Finds and runs the C compiler |
| `src/alku_os.erl` | Runs programs, makes temporary file names |
| `priv/runtime/` | The C runtime: value representation, operators, errors |
| `test/` | Unit tests |
| `examples/` | Example programs with their expected output (also tests) |

## Tests

`rebar3 eunit` runs the unit tests and every program in `examples/`.
Each `NAME.erl` there has a `NAME.expected` file with exactly what the
program should print (or the compile errors it should produce). To add a
test, add a new pair of files.

## License

Alku uses Erlang/OTP's `epp` and `erl_lint` modules at compile time.
Erlang/OTP is licensed under the Apache License 2.0.
