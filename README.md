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

## Roadmap — vision-based defect detection

A future source/action pair worth building: a camera source plugin that
turns Sigil into a small-scale version of an industrial visual-inspection
system (catching manufacturing defects on-device, in real time, no cloud).
It's a natural fit for the existing architecture — one more `SourceBase`
plugin — and it's a good excuse to stress-test the pipeline against a
noisier, higher-throughput signal than the mock sources.

**Scope**

1. `CameraDefectSource` (source plugin) — reads frames from a Pi Camera
   Module, runs a lightweight anomaly-detection model locally, and emits a
   `SignalEvent` (field: `defect_score` or `defect_type`) when a frame
   deviates from the "known good" baseline.
2. Synthetic defect generation for training — instead of hand-labeling
   defective parts, take a handful of photos of a known-good part and
   synthetically composite scratches/dents/discoloration onto them
   (OpenCV) to generate negative examples. Train the anomaly detector on
   that synthetic set.
3. Wire `CameraDefectSource` into the existing `RuleEngine` unmodified —
   a rule like `defect_score > threshold` fires an `ActionBase` (e.g. a
   GPIO alert, a log entry, or a new `NotificationAction`) exactly like
   any other source today.
4. Keep inference on-device (CPU on the Pi 5 initially; a Hailo-8 AI
   accelerator HAT is a later option if CPU inference proves too slow for
   real-time framerates).

**Hardware**

- Pi Camera Module (~$25–35)
- Optional later: Hailo-8 AI accelerator HAT (~$70) for faster inference

**Explicitly out of scope for a first pass:** production-grade throughput,
multi-camera synchronization, and dimension measuring / character reading —
this is meant to prove the pipeline pattern (camera → local inference →
SignalEvent → rule → action), not compete with a production industrial
inspection system.

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
