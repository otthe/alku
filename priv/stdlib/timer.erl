%% The timer module of Alku's standard library, written in Alku.
%% timer:sleep/1 is built into the C runtime.
-module(timer).

-export([hms/3, hours/1, minutes/1, seconds/1, tc/1, tc/2]).

%% {Microseconds, Result}: how long calling Fun took.
tc(Fun) ->
    Start = erlang:monotonic_time(microsecond),
    Result = Fun(),
    {erlang:monotonic_time(microsecond) - Start, Result}.

tc(Fun, Args) ->
    Start = erlang:monotonic_time(microsecond),
    Result = apply(Fun, Args),
    {erlang:monotonic_time(microsecond) - Start, Result}.

%% Durations in milliseconds, as timer:sleep/1 takes them.
seconds(S) -> S * 1000.
minutes(M) -> M * 60000.
hours(H) -> H * 3600000.
hms(H, M, S) -> hours(H) + minutes(M) + seconds(S).
