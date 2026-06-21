## Auto-freeze of remote dynamic bodies (lag-comp Tier 0/2 physics support).
##
## A remote [RigidBody2D] integrates under gravity regardless of network writes,
## so [MultiplayerInterpolator] freezes it kinematic while it is displayed
## remotely and restores the original intent when this peer owns it. No user
## config: a remote dynamic body is never simulated locally.
class_name TestMultiplayerInterpolatorFreeze
extends NetwTestSuite

var _body: RigidBody2D
var _clock: MultiplayerClock
var _tree: MultiplayerTree
var _interpolator: MultiplayerInterpolator
var _sync: MultiplayerSynchronizer


func before_test() -> void:
	_tree = MultiplayerTree.new()
	add_child(_tree)
	auto_free(_tree)

	_clock = MultiplayerClock.new()
	_clock.tickrate = 30
	_clock.display_offset = 0
	_tree.add_child(_clock)
	auto_free(_clock)
	_clock.set_physics_process(false)

	var api := _clock.multiplayer as SceneMultiplayer
	assert(api != null, "test requires SceneMultiplayer")
	api.set_meta(&"_multiplayer_tree", _tree)
	api.set_meta(&"_multiplayer_clock", _clock)

	_body = RigidBody2D.new()
	_body.name = "RemoteBody"
	# A non-local authority makes the interpolator resolve the REMOTE role.
	_body.set_multiplayer_authority(2)
	# Known original intent so the restore is assertable.
	_body.freeze = false
	_body.freeze_mode = RigidBody2D.FREEZE_MODE_STATIC
	auto_free(_body)

	_sync = MultiplayerSynchronizer.new()
	_sync.name = "MultiplayerSynchronizer"
	var cfg := SceneReplicationConfig.new()
	var ppath := NodePath(".:position")
	cfg.add_property(ppath)
	cfg.property_set_replication_mode(
		ppath,
		SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
	)
	_sync.replication_config = cfg
	_sync.set_multiplayer_authority(2)
	_body.add_child(_sync)
	_sync.owner = _body

	_interpolator = MultiplayerInterpolator.new()
	_interpolator.property_modes = { &"position": MultiplayerInterpolator.Mode.LERP }
	_body.add_child(_interpolator)
	_interpolator.owner = _body

	_tree.add_child(_body)

	await get_tree().process_frame


func after_test() -> void:
	var api := _clock.multiplayer as SceneMultiplayer
	if api:
		if api.has_meta(&"_multiplayer_clock"):
			api.remove_meta(&"_multiplayer_clock")
		if api.has_meta(&"_multiplayer_tree"):
			api.remove_meta(&"_multiplayer_tree")
	await super.after_test()


func test_remote_rigidbody_is_frozen_kinematic() -> void:
	assert_int(_interpolator._strategy_role).is_equal(
		MultiplayerInterpolator.DisplayRole.REMOTE,
	)
	assert_bool(_body.freeze).is_true()
	assert_int(_body.freeze_mode).is_equal(RigidBody2D.FREEZE_MODE_KINEMATIC)


func test_restores_original_intent_on_local_authority() -> void:
	# Gaining local authority flips the role off REMOTE; the body must simulate
	# again with its original freeze intent restored.
	_body.set_multiplayer_authority(1)
	_interpolator._role_dirty = true
	_interpolator._resolve_strategy(true)

	assert_int(_interpolator._strategy_role).is_equal(
		MultiplayerInterpolator.DisplayRole.DISABLED,
	)
	assert_bool(_body.freeze).is_false()
	assert_int(_body.freeze_mode).is_equal(RigidBody2D.FREEZE_MODE_STATIC)
