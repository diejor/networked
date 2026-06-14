## Unit tests for [ProxySynchronizer].
##
## Covers property registration with options, read/write dispatch through
## [method _read_property] / [method _write_property] hooks, and
## [method _get_property_list] reporting.
class_name TestProxySynchronizer
extends NetwTestSuite

@warning_ignore("missing_tool")
class StubProxy extends ProxySynchronizer:
	var _store: Dictionary[StringName, Variant] = { }


	func _read_property(_name: StringName, _path: NodePath) -> Variant:
		return _store.get(_name)


	func _write_property(
			_name: StringName,
			_path: NodePath,
			value: Variant,
	) -> void:
		_store[_name] = value


#region Registration

@warning_ignore("unused_parameter")
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
	proxy.register_property(&"health", NodePath(":health"), mode, spawn)
	proxy.finalize()

	var vpath := proxy._virtual_path(&"health")
	assert_that(proxy.replication_config.has_property(vpath)).is_true()
	assert_that(
		proxy.replication_config.property_get_replication_mode(vpath),
	).is_equal(mode)
	assert_that(proxy.replication_config.property_get_spawn(vpath)).is_equal(spawn)
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
	proxy.finalize()
	assert_that(proxy._properties.size()).is_equal(2)
	assert_that(proxy.replication_config.get_properties().size()).is_equal(2)


func test_register_node_property_defers_to_finalize() -> void:
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

	# Before finalize the deferred list has the entry, _properties does not.
	assert_that(proxy._properties.has(&"health")).is_false()
	assert_that(proxy._deferred_node_props.size()).is_equal(1)

	proxy.finalize()

	# After finalize, the real path and virtual config path are resolved.
	assert_that(proxy._properties.has(&"health")).is_true()
	assert_that(proxy._deferred_node_props.size()).is_equal(0)
	var vpath := proxy._virtual_path(&"health")
	assert_that(proxy.replication_config.has_property(vpath)).is_true()


func test_finalize_preserves_imported_flags_for_registered_path() -> void:
	var root: Node2D = auto_free(Node2D.new())
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.name = "Proxy"
	proxy.root_path = NodePath("..")
	root.add_child(proxy)

	var real_path := NodePath(".:position")
	proxy.register_property(&"pos", real_path)

	var imported := SceneReplicationConfig.new()
	imported.add_property(real_path)
	imported.property_set_replication_mode(
		real_path,
		SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
	)
	imported.property_set_spawn(real_path, true)
	imported.property_set_watch(real_path, false)
	proxy.replication_config = imported

	proxy.finalize()

	var vpath := NodePath("Proxy:pos")
	assert_that(proxy.replication_config.has_property(vpath)).is_true()
	assert_that(
		proxy.replication_config.property_get_replication_mode(vpath),
	).is_equal(SceneReplicationConfig.REPLICATION_MODE_ALWAYS)
	assert_that(proxy.replication_config.property_get_spawn(vpath)).is_true()
	assert_that(proxy.replication_config.property_get_watch(vpath)).is_false()

#endregion

#region Read/write dispatch

@warning_ignore("unused_parameter")
func test_set_get_dispatch(
		prop: StringName,
		registered: bool,
		expected_set_result: bool,
		test_parameters := [
			[&"speed", true, true],
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
@warning_ignore("unused_parameter")
func test_property_roundtrip_fuzz(
		fuzzer := Fuzzers.rangei(-1_000_000, 1_000_000),
		fuzzer_iterations := 20,
) -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"v", NodePath(":v"))

	var raw: int = fuzzer.next_value()
	var value := Vector2(float(raw % 1000), floor(float(raw) / 1000.0))

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
	var vpath := proxy._virtual_path(&"score")
	assert_that(proxy.replication_config.has_property(vpath)).is_true()

#endregion
