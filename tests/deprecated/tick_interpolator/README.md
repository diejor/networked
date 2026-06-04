# Deprecated Tick Interpolator Integration Tests

These integration tests are temporarily disabled.

The tick interpolator system is out of scope for the current test suite
compaction work. The live simulation tests have carried meaningful engineering
effort, so they are preserved here instead of deleted. They should be useful
again when tick interpolation becomes a product focus and the surrounding test
architecture can support it cleanly.

The current problem is not only runtime. Since the tick interpolator tests were
introduced, the integration suites have been a recurring source of flaky
failures, noisy windows, and friction between the simulation model and the test
harness. The underlying test infrastructure is improving, and the architecture
around tick interpolation should not be removed just because these tests are
paused.

Before enabling these tests again, refactor them around a more useful version
of the tick interpolation feature. The next pass should make the tests
deterministic, avoid visible runner windows, and separate fast unit coverage
from any slower live network simulation checks.

The disabled integration suites are:

```text
test_tick_interpolator_network.gd
test_tick_interpolator_edge_cases.gd
```

The archived scene fixture is:

```text
tick_test_stage.tscn
```

Remove `.gdignore` only when the suite has been refactored and can run without
adding avoidable runtime, visual noise, or flaky failures to the main test run.

The unit suites remain active:

```text
res://tests/unit/test_tick_interpolator.gd
res://tests/unit/test_tick_interpolator_signals.gd
```
