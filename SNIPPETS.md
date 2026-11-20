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