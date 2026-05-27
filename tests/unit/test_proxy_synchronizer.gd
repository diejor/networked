## Unit tests for [ProxySynchronizer] and [TickAwareSynchronizer].
##
## Covers property registration with options, read/write dispatch through
## [code]_read_property[/code] / [code]_write_property[/code] hooks,
## [code]_get_property_list[/code] reporting, and tick-aware finalize
## semantics.
class_name TestProxySynchronizer
extends NetwTestSuite


@warning_ignore("missing_tool")
class StubProxy extends ProxySynchronizer:
	var _store: Dictionary[StringName, Variant] = {}

	func _read_property(_name: StringName, _path: NodePath) -> Variant:
		return _store.get(_name)

	func _write_property(
		_name: StringName,
		_path: NodePath,
		value: Variant
	) -> void:
		_store[_name] = value


class StubTickAware extends TickAwareSynchronizer:
	var _store: Dictionary[StringName, Variant] = {}

	func _read_property(_name: StringName, _path: NodePath) -> Variant:
		return _store.get(_name)

	func _write_property(
		_name: StringName,
		_path: NodePath,
		value: Variant
	) -> void:
		_store[_name] = value


#region Registration

func test_register_property(
	mode: int,
	spawn: bool,
	test_parameters := [
		# defaults: ON_CHANGE mode, spawn=false
		[SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE, false],
		# explicit: same mode but spawn=true
		[SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE, true],
	],
) -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	var path := NodePath(":health")
	proxy.register_property(&"health", path, mode, spawn)

	assert_that(proxy._config.has_property(path)).is_true()
	assert_that(proxy._config.property_get_replication_mode(path)).is_equal(mode)
	assert_that(proxy._config.property_get_spawn(path)).is_equal(spawn)
	assert_that(proxy._properties.size()).is_equal(1)


func test_register_duplicate_is_ignored() -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"health", NodePath(":health"))
	proxy.register_property(&"health", NodePath(":health"))
	assert_that(proxy._properties.size()).is_equal(1)


func test_register_multiple_properties_track_independently() -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"hp", NodePath(":hp"))
	proxy.register_property(&"mp", NodePath(":mp"))
	assert_that(proxy._properties.size()).is_equal(2)
	assert_that(proxy._config.get_properties().size()).is_equal(2)


func test_register_node_property_uses_proxy_relative_path() -> void:
	var root: Node2D = auto_free(Node2D.new())
	var components := Node.new()
	components.name = "Components"
	root.add_child(components)

	var source := Node.new()
	source.name = "State"
	components.add_child(source)

	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.name = "Proxy"
	proxy.root_path = NodePath(".")
	components.add_child(proxy)

	proxy.register_node_property(&"health", source, &"health")
	assert_that(proxy.get_real_path(&"health")).is_equal(
		NodePath("../State:health")
	)


#endregion

#region Read/write dispatch

func test_set_get_dispatch(
	prop: StringName,
	registered: bool,
	expected_set_result: bool,
	test_parameters := [
		[&"speed",   true,  true],
		[&"unknown", false, false],
	],
) -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	if registered:
		proxy.register_property(prop, NodePath(":" + prop))

	assert_that(proxy._set(prop, 42)).is_equal(expected_set_result)
	if registered:
		assert_that(proxy._store.get(prop)).is_equal(42)
		assert_that(proxy._get(prop)).is_equal(42)
	else:
		assert_that(proxy._get(prop)).is_null()


# Fuzz any registered property through _set -> _store -> _get and assert
# the variant round-trips byte-for-byte.
func test_property_roundtrip_fuzz(
	fuzzer := Fuzzers.rangei(-1_000_000, 1_000_000),
	fuzzer_iterations := 20,
) -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"v", NodePath(":v"))

	var raw: int = fuzzer.next_value()
	var value := Vector2(float(raw % 1000), float(raw / 1000))

	assert_that(proxy._set(&"v", value)).is_true()
	assert_that(proxy._get(&"v")).is_equal(value)


#endregion

#region Inspector and finalize

func test_get_property_list_exposes_registered_names() -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"a", NodePath(":a"))
	proxy.register_property(&"b", NodePath(":b"))
	proxy._store[&"a"] = 0
	proxy._store[&"b"] = 0

	var names: Array[StringName] = []
	for entry in proxy._get_property_list():
		names.append(StringName(entry["name"]))

	assert_that(proxy._get_property_list().size()).is_equal(2)
	assert_that(names.has(&"a")).is_true()
	assert_that(names.has(&"b")).is_true()


func test_finalize_applies_config_to_replication_config() -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"score", NodePath(":score"))
	proxy.finalize()
	assert_that(proxy.replication_config).is_not_null()
	assert_that(
		proxy.replication_config.has_property(NodePath(":score"))
	).is_true()


#endregion

#region Tick-aware synchronizer

func test_finalize_with_tick_inserts_tick_first_always_no_spawn() -> void:
	var sync: StubTickAware = auto_free(StubTickAware.new())
	sync.register_property(&"hp", NodePath(":hp"))
	sync.finalize_with_tick()

	var props := sync.replication_config.get_properties()
	assert_that(props.size()).is_equal(2)
	assert_that(String(props[0])).is_equal(":__tick")
	assert_that(String(props[1])).is_equal(":hp")

	var tick_path := NodePath(":__tick")
	assert_that(
		sync.replication_config.property_get_replication_mode(tick_path)
	).is_equal(SceneReplicationConfig.REPLICATION_MODE_ALWAYS)
	assert_that(
		sync.replication_config.property_get_spawn(tick_path)
	).is_false()


# Tick writes land on [_pending_tick] (not the store), return true, and
# do not interfere with the regular write-through path.
func test_tick_set_dispatch_isolates_tick_from_store() -> void:
	var sync: StubTickAware = auto_free(StubTickAware.new())
	sync.register_property(&"pos", NodePath(":pos"))

	assert_that(sync._set(&"__tick", 42)).is_true()
	assert_that(sync._pending_tick).is_equal(42)
	assert_that(sync._store.has(&"__tick")).is_false()

	sync._set(&"pos", Vector3.ONE)
	assert_that(sync._store.get(&"pos")).is_equal(Vector3.ONE)

#endregion
