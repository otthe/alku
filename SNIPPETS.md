// old

make
make test
bin/alku run test/examples/factorial.erl
bin/alku c test/examples/factorial.erl     # shows the generated C




// test:

rebar3 escriptize
rebar3 eunit
_build/default/bin/alku run examples/factorial.erl


// type alku anywhere:

ln -s "$PWD/_build/default/bin/alku" ~/.local/bin/alku


// IMPORTANT!!!!!!!!!!!!!!!!!!!!!!!

1)
Limitations:
The runtime uses pthreads and aligned_alloc, so Windows will need some work later.
A variable bound inside a case still can’t be used after it.