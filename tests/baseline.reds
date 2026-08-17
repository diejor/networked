# The cases that are red on an unchanged tree.
#
# A gate over `examples/` cannot read an absolute count, because the corpus is
# red before anything is changed and a run that goes green in the wrong place
# looks identical to one that stayed. What a gate reads is the DELTA against
# this file: a red not listed here is a regression, and a listed red that
# passes means this file is stale, which is also worth saying out loud.
#
# Rows are `<verdict> <scope> <case>`. A verdict is `fail` or `skip`.
#
# Recorded and read with the examples scope run alone, into its own report dir
# so a tests-scope run cannot overwrite it:
#
#   godot --path . -s res://addons/gdUnit4/bin/GdUnitCmdTool.gd --headless \
#     --ignoreHeadlessMode --ignore-error-breaks -c -rc 1 \
#     -rd res://reports_scoped -a res://examples
#   .agents/bin/gdunit-report --baseline tests/baseline.reds \
#     reports_scoped/report_1
#
# MEASURED 2026-08-11 at 441f0e9f on the campaign owner's machine, from
# `-a res://examples` run alone, which is the only way it may be run: a single
# invocation carrying both tests and examples exhausts the deferred-call queue
# past ~1,400 cases and SIGSEGVs. 69 cases, 11 fail, 2 skip.
#
# RE-MEASURED 2026-08-12: 69 cases, 14 fail, 1 err, 2 skip. The eleven above
# still hold and none of them went green, so the delta is one case, the
# quick_start round-trip teleport, added below.
#
# THE MARGINAL POLICY, RE-MEASURED 2026-08-11 rather than inherited. Running
# `examples/racing` alone under `NETW_MARGINAL=1`, which un-skips the probes
# excluded by default, gives 40 cases and 10 red, which is these nine plus
# `test_reports_whether_the_gated_residual_stays_bounded_over_a_long_drive`.
# Two things in the recorded policy do not survive that measurement.
#
# The wall-contact probe PASSES. `test_remote_visual_stays_attached_through_a_
# wall_contact` is skipped by default as load-marginal and goes green when it
# is run, so the row below records why it does not execute, not a failure.
#
# The nine are NOT marginal. They are red on an unchanged tree, in both modes,
# and they do not flap between runs the way a load-marginal probe does. Reading
# them as measurement noise is what would let a real regression in the racing
# corpus land unnoticed, so they are listed individually: a tenth red is a
# regression even though nine is the resting state.
#
# THE QUICK_START ROW IS ONE CASE REPORTING FOUR TIMES. It fails three
# assertions and then raises, because the third failure leaves it reading
# `position` off a null. A verdict is keyed by case name, so the one row below
# accounts for all four. It was bisected rather than assumed: the same four
# reports appear, message for message, three commits apart, so the case is red
# on an unchanged tree rather than red because of anything recent.

fail examples/quick_start  test_client_round_trip_teleport_stays_functional

fail examples/racing  test_every_view_of_a_remote_car_converges_on_authority
fail examples/racing  test_prediction_does_not_pull_stale_angular_velocity
fail examples/racing  test_contact_baseline_profile
fail examples/racing  test_contact_with_exact_restore
fail examples/racing  test_reports_same_process_divergence_at_matched_ticks
fail examples/racing  test_reports_correction_trigger_attribution
fail examples/racing  test_reports_position_recovery_across_corrections
fail examples/racing  test_reports_divergence_step_response_at_schedule_faults
fail examples/racing  test_reports_whether_gating_integration_on_the_tick_closes_the_gap
skip examples/racing  test_remote_visual_stays_attached_through_a_wall_contact
skip examples/racing  test_reports_whether_the_gated_residual_stays_bounded_over_a_long_drive
