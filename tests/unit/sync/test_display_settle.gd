## The display runtime's rebuild, observed with no frame.
##
## A config write that invalidates the channel table under an entity does not
## rebuild on the spot: it dirties the runtime and the rebuild lands at the
## session's own settle. These cases pin that cadence and the fact that the
## drain takes every entity dirtied, because a drain that repaired one of them
## would leave the rest dirty with nothing left to raise them again.
class_name TestDisplaySettle
extends NetwTestSuite

const P0 := Vector2(0.0, 0.0)
const P1 := Vector2(100.0, 0.0)

var _tree: MultiplayerTree
var _clock_node: MultiplayerClock
var _clock: ClockCore
var _native_core: NetwMultiplayerCore
var _display: DisplayCore


func before_test() -> void:
	_tree = MultiplayerTree.new()
	_tree.name = "DisplaySettleTree"

	_clock_node = MultiplayerClock.new()
	_clock_node.tickrate = 30
	_clock_node.display_offset = 0
	_clock_node.set_physics_process(false)
	_tree.add_child(_clock_node)

	add_child(_tree)
	auto_free(_tree)

	var api := _tree.api
	api.set_meta(&"_multiplayer_tree", _tree)
	api.set_meta(&"_multiplayer_clock", _clock_node)
	_clock = api._clock
	_native_core = api._native_core
	_display = api._display


func after_test() -> void:
	var api := _tree.api
	if api:
		if api.has_meta(&"_multiplayer_clock"):
			api.remove_meta(&"_multiplayer_clock")
		if api.has_meta(&"_multiplayer_tree"):
			api.remove_meta(&"_multiplayer_tree")
	await super.after_test()


## The rebuild is queued rather than run, and the settle is what lands it, with
## nothing awaited and no frame driven.
func test_a_visual_root_change_rebuilds_at_the_settle_with_no_frame() -> void:
	var entity := _spawn_target("Subject", 7)
	var owner_node := entity.owner
	var first: Node2D = owner_node.get_node("First")
	var second: Node2D = owner_node.get_node("Second")

	entity.interpolation.visual_root = NodePath("Second")
	assert_bool(_rebuild_queued(7)).override_failure_message(
		"a config write that invalidates the channel table must queue a rebuild",
	).is_true()

	_tree.api._settle()

	assert_bool(_rebuild_queued(7)).is_false()

	_feed(owner_node)
	_display_at(1, 1, 0.5)

	assert_vector(second.global_position).is_equal_approx(
		P0.lerp(P1, 0.5),
		Vector2(0.1, 0.1),
	)
	assert_vector(first.global_position).override_failure_message(
		"the visual the rebuild retired must stop receiving the display",
	).is_equal(P0)


## Two runtimes dirtied in one cascade both rebuild, which is what draining the
## whole set buys. A drain that took one entity would leave the first dirtied
## with its flag still raised, and so nothing left to raise it again.
func test_two_entities_dirtied_together_each_rebuild() -> void:
	var one := _spawn_target("SubjectOne", 7)
	var two := _spawn_target("SubjectTwo", 8)

	one.interpolation.visual_root = NodePath("Second")
	two.interpolation.visual_root = NodePath("Second")

	_tree.api._settle()

	_feed(one.owner)
	_feed(two.owner)
	_display_at(1, 1, 0.5)

	assert_vector(one.owner.get_node("Second").global_position) \
			.override_failure_message(
				"the first entity dirtied must not lose its rebuild to the second",
			).is_equal_approx(P0.lerp(P1, 0.5), Vector2(0.1, 0.1))
	assert_vector(two.owner.get_node("Second").global_position) \
			.is_equal_approx(P0.lerp(P1, 0.5), Vector2(0.1, 0.1))


## Verify the three display stages each leave a row of their own kind, keyed to
## the route they ran for. Recording a sample, pumping the runtime and writing a
## visual are three separate acts, and an observer that can only see the last of
## them cannot tell a stream that never arrived from one that arrived and was
## never pumped.
func test_the_display_stages_each_report_on_their_own_route() -> void:
	var entity := _spawn_target("Subject", 7)
	_tree.api._native_core.event_arm(true)

	_feed(entity.owner)
	_display_at(1, 1, 0.5)

	var seen: Dictionary[int, int] = { }
	for row: NetwEvent in _tree.api._native_core.event_ring(7):
		seen[row.event] = int(seen.get(row.event, 0)) + 1
	assert_int(int(seen.get(NetwMultiplayerCore.DISPLAY_RECORD, 0))).is_equal(2)
	assert_int(int(seen.get(NetwMultiplayerCore.DISPLAY_PUMP, 0))).is_equal(1)
	assert_int(int(seen.get(NetwMultiplayerCore.DISPLAY_WRITE, 0))).is_greater(0)
	assert_int(_tree.api._native_core.event_ring(0).size()).is_equal(0)


func test_a_written_param_reads_back_and_an_unknown_entity_answers_nothing() -> void:
	var entity := _spawn_target("Subject", 7)
	var api := _tree.api
	var rid := entity.rid

	assert_int(api.display_get_param(
		rid,
		NetwMultiplayer.DisplayParam.DISPLAY_PARAM_MAX_FORECAST_TICKS,
	)).override_failure_message(
		"an unwritten param must read back its declared default",
	).is_equal(6)

	api.display_set_param(
		rid,
		NetwMultiplayer.DisplayParam.DISPLAY_PARAM_MAX_FORECAST_TICKS,
		5,
	)

	assert_int(api.display_get_param(
		rid,
		NetwMultiplayer.DisplayParam.DISPLAY_PARAM_MAX_FORECAST_TICKS,
	)).override_failure_message(
		"a written param must read back through the flat verb",
	).is_equal(5)

	assert_that(api.display_get_param(
		RID(),
		NetwMultiplayer.DisplayParam.DISPLAY_PARAM_MAX_FORECAST_TICKS,
	)).override_failure_message(
		"an entity the session does not know answers nothing",
	).is_null()


## The handle and the flat verb write ONE record. A setting authored before the
## session knows the entity is what the book publishes for it, and a setting
## written through the flat verb after it is what the handle reads back. Two
## records would let the two spellings disagree about one entity.
func test_the_handle_and_the_flat_verb_write_one_record() -> void:
	var entity := _spawn_target("Subject", 7)
	var api := _tree.api
	var rid := entity.rid

	assert_that(api.display_get_param(
		rid,
		NetwMultiplayer.DisplayParam.DISPLAY_PARAM_VISUAL_ROOT,
	)).override_failure_message(
		"a setting authored before liveness must be what the book publishes",
	).is_equal(NodePath("First"))

	api.display_set_param(
		rid,
		NetwMultiplayer.DisplayParam.DISPLAY_PARAM_MAX_FORECAST_TICKS,
		4,
	)

	assert_int(entity.interpolation.max_forecast_ticks) \
			.override_failure_message(
				"a flat write must read back on the entity's own handle",
			).is_equal(4)


# Whether the runtime on [param route] is waiting for its rebuild.
func _rebuild_queued(route: int) -> bool:
	var runtime := _tree.api._native_core.display_book.runtime_at(route)
	return runtime != null and runtime.rebuild_queued


# A remote-authority Node2D carrying two candidate visual roots, live on
# [param route] with its position interpolated into whichever one is configured.
func _spawn_target(node_name: String, route: int) -> NetwEntity:
	var body := Node2D.new()
	body.name = node_name
	var first := Node2D.new()
	first.name = "First"
	body.add_child(first)
	var second := Node2D.new()
	second.name = "Second"
	body.add_child(second)

	var entity := NetwEntity.ensure(body)
	entity.interpolation.enable_smart_dilation = false
	entity.interpolation.display_role = NetwDisplayHandle.DisplayRole.REMOTE
	entity.interpolation.visual_root = NodePath("First")
	_tree.add_child(body)
	auto_free(body)

	Netw.configure_property(body, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"position"),
	)
	_native_core.liveness_bind_route(route, entity)
	return entity


func _feed(node: Node) -> void:
	_display.record(node, &"position", P0, 0)
	_display.record(node, &"position", P1, 1)


func _display_at(tick: int, display_offset: int, factor: float) -> void:
	_clock.tick = tick
	_clock.display_offset = display_offset
	_clock.tick_factor = factor
	_display.pump(0.0)
