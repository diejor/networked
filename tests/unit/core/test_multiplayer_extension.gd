## Construction and gate laws for the [NetwMultiplayer] implementation door.
class_name TestMultiplayerExtension
extends NetwTestSuite

const CALLTHROUGH := preload(
	"res://tests/support/flat/callthrough_multiplayer_extension.gd"
)
const FLIPPING := preload(
	"res://tests/support/flat/flipping_multiplayer_extension.gd"
)

var _setting_before: Variant


## Preserve the process-wide construction setting around every law.
func before_test() -> void:
	_setting_before = ProjectSettings.get_setting(
		NetwMultiplayer.MULTIPLAYER_SCRIPT_SETTING,
		null,
	)


## Restore the process-wide construction setting.
func after_test() -> void:
	ProjectSettings.set_setting(
		NetwMultiplayer.MULTIPLAYER_SCRIPT_SETTING,
		_setting_before,
	)
	super.after_test()


## Verify an explicit extension script owns the constructed api.
func test_make_constructs_an_explicit_extension() -> void:
	var inner := SceneMultiplayer.new()
	var api := NetwMultiplayer.make(inner, CALLTHROUGH)

	assert_object(api.get_script()).is_same(CALLTHROUGH)
	assert_object(api.inner).is_same(inner)
	api.embedding.dispose()


## Verify the project setting selects the default implementation.
func test_make_reads_the_project_setting() -> void:
	ProjectSettings.set_setting(
		NetwMultiplayer.MULTIPLAYER_SCRIPT_SETTING,
		CALLTHROUGH,
	)
	var api := NetwMultiplayer.make()

	assert_object(api.get_script()).is_same(CALLTHROUGH)
	api.embedding.dispose()


## Verify one tree can override the process-wide implementation.
func test_tree_api_script_overrides_the_project_setting() -> void:
	ProjectSettings.set_setting(
		NetwMultiplayer.MULTIPLAYER_SCRIPT_SETTING,
		FLIPPING,
	)
	var mt := MultiplayerTree.new()
	mt.api_script = CALLTHROUGH
	add_child(mt)
	auto_free(mt)

	assert_object(mt.api.get_script()).is_same(CALLTHROUGH)


## Verify a call-through gate reaches the installed extension unchanged.
func test_callthrough_gate_preserves_the_stock_verdict() -> void:
	var mt := _tree_with(CALLTHROUGH)
	var api = mt.api

	var verdict: Error = api._drive_carrier(
		2,
		NetwFrameEnvelope.pack(
			99,
			0,
			NetwFrameEnvelope.Channel.SYNC,
			PackedByteArray([0, 0]),
		),
		true,
	)

	assert_int(verdict).is_equal(ERR_DOES_NOT_EXIST)
	assert_int(api.sync_calls).is_equal(1)
	assert_int(
		api.get_stat(NetwMultiplayer.Stat.STAT_VERDICT_DOES_NOT_EXIST),
	).is_equal(1)


## Verify an extension can replace a stock accepted verdict before mutation.
func test_flipping_gate_rejects_a_stock_accepted_frame() -> void:
	var mt := _tree_with(FLIPPING)
	var api = mt.api
	var owner := Node2D.new()
	mt.add_child(owner)
	auto_free(owner)
	var entity := NetwEntity.ensure(owner)
	api.entity_bind_route(api.entity_of(owner), 7)

	var verdict: Error = api._drive_carrier(
		2,
		NetwFrameEnvelope.pack(
			7,
			0,
			NetwFrameEnvelope.Channel.SYNC,
			PackedByteArray([0, 0]),
		),
		true,
	)

	assert_int(verdict).is_equal(ERR_UNAUTHORIZED)
	assert_int(api.sync_calls).is_equal(1)
	assert_int(
		api.get_stat(NetwMultiplayer.Stat.STAT_VERDICT_UNAUTHORIZED),
	).is_equal(1)
	assert_int(api.entity_get_state(entity.rid)).is_equal(
		NetwMultiplayer.EntityState.LIVE,
	)


# Builds one mounted tree with an explicit implementation.
func _tree_with(script: Script) -> MultiplayerTree:
	var mt := MultiplayerTree.new()
	mt.api_script = script
	add_child(mt)
	auto_free(mt)
	return mt
