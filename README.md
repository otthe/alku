# Alku

Alku is an ahead-of-time compiler for **sequential Erlang**, written by Opus 5.5. It turns
Erlang source files into standalone native executables, by way of C.

It is not a new language. Alku accepts a subset of Erlang: everything
except processes and OTP. Programs that compile with Alku are ordinary
Erlang, and they print the same output when run on the BEAM. The output
is a single binary that runs without Erlang installed.

## Requirements

- Erlang/OTP 26 or newer and rebar3, to build and run the compiler
- gcc or clang

The executables Alku builds need neither of them.

## Usage

```
rebar3 escriptize                 # builds _build/default/bin/alku
rebar3 eunit                      # runs the tests

alku run FILE.erl [ARGS...]       # compile and run
alku build FILE.erl [-o OUT]      # build an executable
alku c FILE.erl                   # show the generated C
```

`_build/default/bin/alku` is a single file with the C runtime inside it,
so you can link it onto your PATH:
`ln -s "$PWD/_build/default/bin/alku" ~/.local/bin/alku`.

The program starts at `main/1`, which gets the command-line arguments as
strings; `main/0` also works.

```erlang
-module(hello).
-export([main/1]).

main(Args) ->
    io:format("Hello, ~s!~n", [string:join(Args, " ")]).
```

```
$ alku build hello.erl && ./hello Alku
Hello, Alku!
```

When a program uses another module, `M.erl` is compiled from the same
directory. Alku notices the module from a call like `shapes:area(X)`, or
from any mention of the atom `shapes`.

## Supported

All of sequential Erlang, apart from the gaps listed below:

- **Data types:** integers of any size, floats, atoms, tuples, lists,
  strings, maps, records, binaries and bitstrings (bit syntax including
  `utf8`/`utf16`/`utf32`), funs and references
- **Patterns and guards** everywhere, including `"GET " ++ Path` patterns
- **Control flow:** `case`, `if`, `try`/`catch`/`after`, `catch`;
  list, binary and map comprehensions; funs and named funs
- **Calls:** calls chosen at run time (`Mod:Fun(Args)`, `apply/3`,
  `fun M:F/A`), and proper tail calls in every tail position
- **Modules:** several modules with export checks, macros and
  `-include` through Erlang's own preprocessor
- **Errors:** Erlang's error reasons, and stack traces with file and line
- **Memory:** garbage collection, and a 512 MB stack for deep recursion
- **Built-ins:** `term_to_binary`/`binary_to_term` (the same bytes as
  OTP 26), and most of the `erlang` module's sequential built-in functions

## Not supported

- **Concurrency and OTP:** `spawn`, `receive`, `self()`, links, monitors,
  and behaviours (`gen_server` and friends). `receive` is a compile error
  saying so.
- **Language features:** the process dictionary (`put/get/erase`) and
  `maybe` expressions
- **Formatting:** `~g` in `io:format`. `~p` never breaks long terms
  over several lines.
- **phash2:** `erlang:phash2` is stable between runs, but its numbers
  differ from Erlang's.
- **term_to_binary:** cannot encode funs other than `fun M:F/A`, and
  doesn't read compressed terms.
- **Map printing:** maps with more than 32 keys print in key order (Erlang
  prints them in hash order).

## Standard library

The standard library is either written in Erlang and compiled with your
program (`priv/stdlib/`), or provided directly by the C runtime.

| Module | Status |
|---|---|
| `lists` | most of the module |
| `maps` | most of the module |
| `string` | most of the module, on lists and binaries |
| `proplists` | the common functions |
| `io` | `format`, `fwrite`, `put_chars`, `write`, `nl`, `get_line` |
| `io_lib` | `format`, `fwrite` |
| `file` | `open`, `close`, `read_file`, `write_file` |
| `binary` | `split`, `part`, `at`, `first`, `last`, `bin_to_list`, `list_to_bin` |
| `unicode` | `characters_to_list`, `characters_to_binary` |
| `math` | all of it |
| `timer` | `sleep`, `tc`, `seconds`/`minutes`/`hours`/`hms` |
| `os`, `rand` | `getenv`, `timestamp`; `uniform` |

Still missing:

- **Data structures:** `queue`, `sets`, `ordsets`, `gb_trees`,
  `gb_sets`, `dict`, `orddict`, `array`
- **Text:** `re` (regular expressions), `base64`, `uri_string`, `json`
- **Files and the OS:** most of `file` (`list_dir`, `delete`, `consult`,
  `read_line`, ...), `filename`, `filelib`
- **Time:** `calendar`
- **Networking:** `gen_tcp`, `gen_udp`, `inet`
- **Others:** `ets`, `crypto`

## How it works

1. Erlang's own `epp`, `erl_lint` and `erl_expand_records` read and
   check the source.
2. `src/alku_codegen.erl` turns each function into C.
3. The C compiler links the result with the runtime in `priv/runtime/`.

The runtime contains values, bignums, maps (persistent trees), binaries,
the external term format, exceptions and a mark-sweep garbage collector.

Tests: each `examples/NAME.erl` has an `examples/NAME.expected` with its
output. `rebar3 eunit` runs every example twice, normally and with the
garbage collector running constantly (`ALKU_GC_STRESS=1`).
