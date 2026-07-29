# Sigil — Signal Daemon

Sigil is a local-first rule engine that connects signals to actions: a sensor
reading, a radio detection, or any other physical-world event flows in as a
`SignalEvent`, gets matched against rules, and fires an action — all
on-device, no cloud round-trip.

## How it works

```
Source Layer   →   Signal Bus            →   Rule Engine   →   Action Layer
(one plugin        (lock-free MPSC             (matches          (one plugin
 per input,         ring buffer;                 SignalEvents      per effect:
 Pull/Push/         many producer                against           GPIO, Display,
 Stream/Bidi/        threads, one                  registered        Network, File)
 Batch)              consumer)                    rules)
```

1. A source plugin emits a `SignalEvent` — the one struct every source
   normalizes to (a name, a field, a `float`/`string`/`bool` value, a
   timestamp, and a metadata map).
2. The event is published onto the `SignalBus`, a bounded, lock-free
   multi-producer/single-consumer ring buffer, so source threads never
   block on the rule engine and vice versa.
3. The `RuleEngine` drains the bus and evaluates every event against
   registered `Rule`s (a `Condition` on one source/field, with an optional
   cooldown so a rule doesn't refire on every single matching event).
4. On a match, it builds an `ActionRequest` and dispatches it to the
   matching `ActionBase` plugin, which resolves `{field}` placeholders in
   its message template against the triggering event and executes.

## Status

**Phase 1 — core pipeline with mock data — is complete.** Every piece above
is implemented, unit tested, and wired together in `src/main.cpp`, running
end to end with synthetic sources (`MockHumiditySource`, a polled sensor
whose value random-walks so rules have something to react to;
`MockMotionSource`, an event-driven source running its own thread — the
one thing that actually proves the bus's concurrent-producer path outside
of a test) and a synthetic action (`ConsoleAction`, which prints the
resolved message to stdout).

Not yet started, by design — phase 1 deliberately stays hardware- and
LLM-free:
- Real source/action plugins (DHT22, ADS-B, GPIO, notifications, ...)
- An AST layer and LLM integration for natural-language rules
- A networked/multi-device signal bus (today's is strictly in-process)

## Building

Requirements: CMake, a C++20 compiler, and internet access on first
configure (dependencies are fetched via `FetchContent` — nothing to
install manually).

```sh
cmake --preset default
cmake --build --preset default
```

## Testing

```sh
ctest --preset default
```

Or run an individual suite directly, e.g. `./build/bin/signal_bus_tests`.
Each module (`signal_bus`, `action_base`, `rule_engine`, `mock_sources`,
`console_action`) is its own GoogleTest executable, filterable via
`ctest -L <label>` or `--gtest_filter=<Suite>.<Test>`.

## Running

```sh
./build/bin/sigil
```

Starts the daemon with the mock sources and `ConsoleAction` wired up;
press Ctrl+C to stop.

## Project layout

```
include/daemon/   public headers: SignalEvent, SignalBus, SourceBase,
                  ActionBase, RuleEngine, mock plugins
src/              daemon executable (sigil_core library + main.cpp)
tests/            GoogleTest suite, one executable per module
cmake/            FetchContent dependency declarations
docs/             Doxygen-generated documentation source
```

## Documentation

Generate the full Doxygen site (architecture, the signal bus's concurrency
design, the source/action interfaces, and implementation status) from the
repo root:

```sh
doxygen Doxyfile
```

Output goes to `docs/html/` (gitignored — generated, not committed).
