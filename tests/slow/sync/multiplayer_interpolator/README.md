# Deprecated Multiplayer Interpolator Integration Tests

These integration tests are temporarily disabled.

The current problem is not only runtime. Since the multiplayer interpolator tests were
introduced, the integration suites have been a recurring source of flaky
failures, noisy windows, and friction between the simulation model and the test
harness. The underlying test infrastructure is improving, and the architecture
around multiplayer interpolation should not be removed just because these tests are
paused.

Before enabling these tests again, refactor them around a more useful version
of the multiplayer interpolation feature. The next pass should make the tests
deterministic, avoid visible runner windows, and separate fast unit coverage
from any slower live network simulation checks.

The archived integration suites are:

```text
archived_multiplayer_interpolator_network.gd.disabled
archived_multiplayer_interpolator_edge_cases.gd.disabled
```

The unit suites remain active:

```text
res://tests/unit/sync/test_multiplayer_interpolator.gd
res://tests/unit/sync/test_multiplayer_interpolator_signals.gd
```
