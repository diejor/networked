# Networked Test

Companion testing library for the [Networked](../networked/) addon.

This is a **script library**, not a Godot `EditorPlugin`; there is no
`plugin.cfg` or `plugin.gd`. The three `class_name`-registered globals
become available as soon as the directory is present in your project.

## Public API

- **`NetwTestHarness`** - Multi-peer rig (1 server + N clients in-process)
  built on `LocalLoopbackSession`. Use directly from any test, or via
  `NetwTestSuite.make_harness()` for auto-cleanup.
- **`NetwTestSuite`** - Base class for tests that need `timeout_await`,
  `wait_until`, log scopes, and the harness factory. Extends
  `GdUnitTestSuite`.
- **`NetwTestSessionHook`** - GdUnit4 session hook that resets the `Netw`
  debugger between tests and detects root-node leaks. Register in project
  settings under `gdunit4/settings/test/hooks`.

## Framework

The harness core is framework-agnostic. `gdunit4/` adapts it to GdUnit4 via
a `Callable` awaiter. Other frameworks (or no framework at all) can assign
their own timeout reporter to `NetwTestHarness.awaiter`.

## Manual

The full testing guide lives in `docs/manual/testing.rst`.

## Dependency

Requires `addons/networked/` to be present.
