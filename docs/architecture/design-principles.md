# Design principles

Source comments refer to these principles by number, so existing numbers are
kept when wording is updated.

| # | Principle | Current mechanism |
|---|---|---|
| 1 | Keep platform APIs out of `src/core` | The native environment can see only core libraries |
| 2 | Keep dependencies directed from adapters toward core | Component `REQUIRES` declarations and invariant check |
| 3 | Generate capabilities from registered commands | `CommandRegistry::write_capabilities` |
| 4 | Bound waits with deadlines | `runtime::Deadline` and per-operation limits |
| 5 | Deliver cancellation independently of work queues | Atomic `CancellationSource` |
| 6 | Make shared runtime state explicit | Atomic snapshots for conversation/cancellation and controlled refresh paths |
| 7 | Test hardware-independent behavior on the host | Native test suites under `src/test` |

## 1. Keep platform APIs out of `src/core`

`src/core` contains the protocol, parsing, state transitions, policies, and
ports. The `native` environment builds only this directory, so a dependency
on a platform header fails during a host build.

Code that reads a pin, starts a task, or calls a device SDK belongs in
`src/platform`. A decision that can be expressed without hardware can be kept
in `src/core`.

## 2. Keep dependencies directed toward core

Platform components may implement interfaces from
`src/core/stackchan_ports`. Core components do not reference platform
components. `src/main` is the composition root that constructs both sides and
connects them.

Run `python tools/check-invariants.py` after changing component dependencies.

## 3. Generate capabilities from registered commands

A command is registered once with its name, parameters, availability, and
handler. `device.describe` generates its capability list from the same
registry.

Hardware-dependent commands remain discoverable when unavailable. Their
capability has `available: false` and a reason.

## 4. Bound waits with deadlines

Network, audio, scan, and playback operations need an upper bound. A loop also
needs an overall deadline; limiting each individual read is not sufficient if
the loop can repeat indefinitely.

Use `runtime::Deadline` for hardware-independent deadline arithmetic and pass
the remaining limit into lower-level operations.

## 5. Deliver cancellation independently of queues

Conversation cancellation and emergency stop use an atomic cancellation
source. Long-running work checks its token between bounded operations. This
allows cancellation to be observed even when another queue or resource is
busy.

An ordinary cancellation is cleared after the current conversation. An
emergency stop remains active until `estop.clear` or a device restart.

## 6. Make shared runtime state explicit

The conversation phase and cancellation reason are published atomically.
`DeviceState` is refreshed from those values and from the network status in
the UI and command paths.

When adding shared state, define:

- which task or component updates it
- how other tasks obtain a consistent value
- whether atomic access, a lock, or a message is required
- when the value is reset

Avoid introducing mutable globals without a documented owner and access rule.

## 7. Test hardware-independent behavior on the host

Add or update a native test for protocol, parsing, timing, and state behavior
that does not require a peripheral. Test directory names must start with
`test_native_` to match the configured filter.

Hardware adapters still need physical verification. Record the environment,
board/base, observed result, and relevant redacted log in the pull request.
