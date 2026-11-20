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

alku run FILE.erl [ARGS...]       # compile and run
alku build FILE.erl [-o OUT]      # build an executable (default: FILE)
alku c FILE.erl                   # show the generated C code
```

`_build/default/bin/alku` is a single file with the C runtime inside, so
you can put it on your PATH, e.g.
`ln -s "$PWD/_build/default/bin/alku" ~/.local/bin/alku`.

## What works now (phase 6)

- Integers of any size (small ones are fast; big ones grow as needed),
  floats, atoms, tuples, lists, strings (lists of character codes, as in
  Erlang), binaries, maps, records and funs
- **Programs of several modules**: when a module calls `shapes:area(X)`,
  `alku` compiles `shapes.erl` from the same directory too (and the
  modules that one uses). Calls to unexported functions and missing
  modules are compile errors. `-import` works.
- **Binaries and the bit syntax**: `<<1, 300:16, X/binary>>`, patterns
  like `<<Len:16, Data:Len/binary, Rest/binary>>`, segments of any bit
  size, `integer`/`float`/`binary`/`utf8`, `signed`, `little`, `unit:N`,
  binary comprehensions `<< <<X>> || <<X>> <= B >>` and binary generators.
  Matching shares memory instead of copying. A whole binary must be a
  whole number of bytes (no bitstrings)
- Functions with multiple clauses, pattern matching and guards (`,` and `;`)
- Patterns: variables, literals, `{...}`, `[H | T]`, `"abc"`, `P1 = P2`,
  repeated variables
- `case`, `if`, `begin ... end`, `=` matches; variables bound in every
  clause of a `case`/`if` can be used after it
- Exceptions: `try ... of ... catch ... after ... end`, `catch Expr`,
  `throw/1`, `error/1,2`, `exit/1`. Runtime errors are raised with
  Erlang's reasons (`badarith`, `{badmatch, V}`, `function_clause`, ...),
  and a guard that raises is simply false, as in Erlang
- List comprehensions: `[X * 2 || X <- L, X > 0]`, several generators,
  patterns in generators
- Funs: `fun(X) -> ... end` with captured variables, named funs
  (`fun Loop(N) -> ... Loop(N - 1) end`), `fun f/1`, `fun lists:map/2`,
  `fun erlang:abs/1`
- **Proper tail calls**: any call in tail position (to the same function,
  another function, or a fun; inside `case`, `if`, `andalso`, `orelse`)
  runs in constant stack space. Non-tail recursion can go very deep too:
  programs run on a 512 MB stack.
- **Garbage collection**: memory that is no longer used is reclaimed
- Operators: `+ - * / div rem`, `band bor bxor bnot bsl bsr`, comparisons
  (full Erlang term order; `1 == 1.0` but not `1 =:= 1.0`),
  `and or xor not andalso orelse`, `++ --`
- Maps: `#{K => V}`, `M#{K := V}`, patterns `#{K := V}`, map
  comprehensions `#{K => V || K := V <- M}` and map generators in list
  comprehensions
- Records: `-record`, `#r{}`, `R#r.f`, `R#r{f = V}`, record patterns,
  `record_info/2`, `is_record/2` (expanded into tuples by Erlang's own
  `erl_expand_records`)
- Built-ins: `is_integer/1`, `is_atom/1`, `is_boolean/1`, `is_list/1`,
  `is_tuple/1`, `is_function/1,2`, `abs/1`, `hd/1`, `tl/1`, `length/1`,
  `element/2`, `setelement/3`, `tuple_size/1`, `integer_to_list/1`,
  `atom_to_list/1`, `list_to_integer/1`, `list_to_atom/1`,
  `tuple_to_list/1`, `list_to_tuple/1`, `max/2`, `min/2`, `apply/2`,
  `halt/0,1`, `is_float/1`, `is_number/1`, `float/1`, `round/1`,
  `trunc/1`, `floor/1`, `ceil/1`, `float_to_list/1,2`, `list_to_float/1`,
  `integer_to_list/2`, `list_to_integer/2`, `is_map/1`, `map_size/1`,
  `is_map_key/2`, `map_get/2`
- `math`: `pi sqrt pow exp log log2 log10 sin cos tan asin acos atan atan2
  floor ceil`
- `maps`: `new get put update remove is_key find keys values to_list
  from_list size merge fold map filter foreach update_with with without`
- Binaries: `is_binary/1`, `byte_size/1`, `bit_size/1`,
  `binary_to_list/1`, `list_to_binary/1`, `iolist_to_binary/1`,
  `iolist_size/1`, `binary_part/3`, `split_binary/2`, `atom_to_binary/1,2`,
  `binary_to_atom/1,2`, `integer_to_binary/1,2`, `binary_to_integer/1,2`,
  `binary:split/2,3 part/3 at/2 first/1 last/1`,
  `unicode:characters_to_binary/1`, `unicode:characters_to_list/1`
- Programs: `main/1` receives the command-line arguments as strings
  (`main/0` also works)
- I/O: `io:format/1,2,3`, `io:put_chars/1,2`, `io:write/1,2`,
  `io:nl/0,1`, `io:get_line/1,2`, `file:open/2` (read, write, append),
  `file:close/1`, `file:read_file/1`, `file:write_file/2` (which writes
  bytes, as in Erlang: use `unicode:characters_to_binary/1` for text)
- Standard library `lists`: `all any append duplicate filter flatten foldl
  foldr foreach last map max member min nth reverse seq sort sum zip`
- Standard library `string`: `is_empty join lowercase split tokens
  to_lower to_upper trim uppercase`
- `io:format` controls: `~w ~p ~s ~c ~b ~B ~f ~e ~i ~n ~~` with field
  width, precision and padding (`~5B`, `~-10s`, `~.2f`, `~2..0B`, `~.16B`)
- Erlang-style runtime errors (`no function clause matching f(5)`, ...)
- Compile errors and warnings from Erlang's own linter

Programs that use only these features are also valid Erlang and print
the same output under `erl`. Differences: `~p` never breaks long terms
over several lines, and maps with more than 32 keys print in key order
(Erlang uses hash order for those).

Not yet: bitstrings, utf16/utf32 segments, stack traces, `"abc" ++ Rest`
patterns, `~g`, string functions on binaries.

## How the runtime works

- **Values** are small C structs passed by value: a tag plus an integer,
  a float, an atom name or a pointer to a heap object (big integer,
  tuple, list cell, fun, map node).
- **Numbers** (`alku_number.c`): integers that fit in 64 bits are stored
  inline; results that don't become big integers (32-bit limbs, Knuth's
  long division). Floats print exactly as Erlang prints them.
- **Maps** (`alku_map.c`): persistent weight-balanced trees, so an update
  costs O(log n) and never changes the old map.
- **Binaries** (`alku_binary.c`): a binary owns its bytes, or (when made by
  matching) points into another binary's bytes, so `<<X, Rest/binary>>`
  loops run in linear time.
- **Calls**: every Alku function and fun body is a C function taking its
  arguments as an array. A call in tail position to the function itself
  becomes a `goto` back to its start. Any other tail call returns a
  "pending call" to the nearest non-tail call site, which runs it
  (a trampoline), so the C stack never grows.
- **Exceptions** use `setjmp`/`longjmp`: `try` pushes a handler on the C
  stack and raising jumps to the innermost one.
- **Garbage collector** (`alku_gc.c`): mark-and-sweep. Roots are found by
  scanning the C stack conservatively; heap objects are traced precisely.
  Set `ALKU_GC_STRESS=1` to collect very often, which the tests do to
  catch memory bugs.

## How it is organised

| Path | What it does |
|---|---|
| `src/alku.erl` | Command-line tool |
| `src/alku_compiler.erl` | The pipeline: finds the program's modules, then frontend → codegen → C compiler |
| `src/alku_frontend.erl` | Parses with `epp` and checks with `erl_lint` |
| `src/alku_codegen.erl` | Turns Erlang abstract syntax into C (tail calls, funs) |
| `src/alku_c.erl` | Helpers for C names, literals and indentation |
| `src/alku_cc.erl` | Finds and runs the C compiler |
| `src/alku_os.erl` | Runs programs, makes temporary file names |
| `src/alku_priv.erl` | Reads files from `priv/` (also inside the escript) |
| `src/alku_stdlib.erl` | Loads the standard library modules |
| `priv/runtime/` | The C runtime: values, numbers, maps, calls, errors, GC |
| `priv/stdlib/` | The standard library, written in Alku |
| `test/` | Unit tests |
| `examples/` | Example programs with their expected output (also tests); `.erl` files without `.expected` are helper modules |

## Tests

`rebar3 eunit` runs the unit tests and every program in `examples/`,
once normally and once with `ALKU_GC_STRESS=1`. A program can also
have `NAME.input` (given as stdin) and `NAME.args` (command-line
arguments, one per line).
Each `NAME.erl` there has a `NAME.expected` file with exactly what the
program should print (or the compile errors it should produce). To add a
test, add a new pair of files.

## License

Alku uses Erlang/OTP's `epp` and `erl_lint` modules at compile time.
Erlang/OTP is licensed under the Apache License 2.0.
