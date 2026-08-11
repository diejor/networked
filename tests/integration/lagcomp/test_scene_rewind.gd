## Integration tests for scoped scene rewind
## ([method NetwMultiplayer.lagcomp_rewind]).
##
## What remains here is the one edge a declared world cannot reach: a target
## that lingers past its own despawn, whose window is a [SceneTree] timer.
## Driven server-only through [RewindScenario].
class_name TestSceneRewind
extends NetwTestSuite


func test_linger_keeps_target_rewindable_until_freed() -> void:
	var s := RewindScenario.new()
	await s.setup(self)
	var node := await s.spawn_despawnable_entity("Linger")
	var entity := NetwEntity.of(node)

	# Registered by state-set presence on the server. Seed a known
	# authoritative state so the rewind query is meaningful after despawn.
	var tl := s.server.api._lagcomp.timeline_of(entity)
	assert_that(tl).is_not_null()
	tl.record_state(5, { &"position": Vector2(40.0, 0.0) })

	# Despawn with linger: the target dies but stays rewindable for the window.
	var opts := NetwEntity.DespawnOpts.new(&"killed")
	opts.linger = true
	opts.linger_seconds = 0.15
	NetwEntity.of(node).despawn(opts)
	await (Engine.get_main_loop() as SceneTree).process_frame

	# During the window the entity lingers (deactivated, not freed) and stays
	# rewindable, so a late shooter still validates against where the target was.
	assert_bool(is_instance_valid(node)).is_true()
	assert_that(s.server.api._lagcomp.timeline_of(entity)).is_not_null()
	var rid := s.server.api.rid_of(node)
	var during := s.server.api.lagcomp_sample(rid, 5)
	assert_vector(during.position).is_equal_approx(Vector2(40.0, 0.0), Vector2.ONE * 0.001)

	# After the window passes the entity frees and its timeline unregisters.
	await (Engine.get_main_loop() as SceneTree).create_timer(0.35).timeout
	await (Engine.get_main_loop() as SceneTree).process_frame
	assert_bool(is_instance_valid(node)).is_false()
	assert_that(s.server.api._lagcomp.timeline_of(entity)).is_null()
	assert_bool(s.server.api.lagcomp_sample(rid, 5).is_empty()).is_true()
