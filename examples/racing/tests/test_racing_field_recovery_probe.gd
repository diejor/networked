extends NetwTestSuite
## Throwaway probe: prints PredictionHandle.field_recovery per state field after
## a drive that actually produces corrections, to decide whether the two
## teleport_only() solver velocities are worth the recoveries they demand.

const MAIN := preload("res://examples/racing/main.tscn")

const CONTACT_DISTANCE := 2.0
const DRIVE_TICKS := 480
const SETTLE_TICKS := 300

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()


func after_test() -> void:
	if is_instance_valid(game):
		await game.teardown()
	await super.after_test()


func _report(label: String, handle) -> void:
	print("[fieldrec:%s] corrections=%d" % [label, handle.stats.corrections])
	var keys: Array = handle.field_recovery.keys()
	keys.sort()
	print("[fieldrec:%s] %-26s %9s %9s %9s %8s %9s" % [
		label, "field", "triggered", "repaired", "contracted", "carried", "declined",
	])
	for field: StringName in keys:
		var row = handle.field_recovery[field]
		print("[fieldrec:%s] %-26s %9d %9d %9d %8d %9d" % [
			label,
			String(field),
			row.triggered,
			row.repaired,
			row.contracted,
			row.carried,
			row.declined,
		])


# Drives own at target with the contact probe's bang-bang aim, then releases.
func _drive(own: Node, target: Node) -> void:
	var inputs: Node = own.inputs
	var min_distance := INF
	inputs.state[inputs.accelerate] = true
	for i in DRIVE_TICKS:
		var to_target: Vector3 = target.sphere_position - own.sphere_position
		var want := atan2(to_target.x, to_target.z)
		var error := angle_difference(own.heading, want)
		inputs.state[inputs.steer_left] = error > 0.05
		inputs.state[inputs.steer_right] = error < -0.05
		await game.sync_ticks(1)
		min_distance = minf(
			min_distance,
			(own.sphere_position - target.sphere_position).length(),
		)
	inputs.state[inputs.accelerate] = false
	inputs.state[inputs.steer_left] = false
	inputs.state[inputs.steer_right] = false
	await game.sync_ticks(SETTLE_TICKS)
	print("[fieldrec] min_dist=%.2f" % min_distance)
	assert_float(min_distance) \
			.override_failure_message(
				"the drive never reached the other car (min %.2fm)" % min_distance,
			).is_less(CONTACT_DISTANCE)


func test_field_recovery_counts_over_a_contact_drive() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)
	var own := await client.await_player(&"luigi", 2.0)
	var target := await client.await_player(&"mario", 2.0)
	await game.sync_ticks(12)
	await _drive(own, target)
	_report("contact", own.entity.prediction)


func test_field_recovery_counts_over_a_free_drive() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)
	var own := await client.await_player(&"luigi", 2.0)
	await client.await_player(&"mario", 2.0)
	await game.sync_ticks(12)

	var inputs: Node = own.inputs
	inputs.state[inputs.accelerate] = true
	inputs.state[inputs.steer_right] = true
	await game.sync_ticks(DRIVE_TICKS)
	inputs.state[inputs.accelerate] = false
	inputs.state[inputs.steer_right] = false
	await game.sync_ticks(SETTLE_TICKS)
	_report("free", own.entity.prediction)
