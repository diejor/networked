## Unit tests for [ProxySynchronizer].
##
## Covers property registration, read/write dispatch, and property-list
## reporting.
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


@warning_ignore("missing_tool")
class StubStampedProxy extends StubProxy:
	func _ordered_virtual_names() -> Array[StringName]:
		return [&"__tick"]


func test_registration_flow() -> void:
	for spawn in [false, true]:
		var proxy: StubProxy = auto_free(StubProxy.new())
		proxy.register_property(
			&"health",
			NodePath(":health"),
			SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
			spawn,
		)
		proxy.finalize()

		var vpath := proxy._virtual_path(&"health")
		assert_that(proxy.replication_config.has_property(vpath)).is_true()
		assert_that(
			proxy.replication_config.property_get_replication_mode(vpath),
		).is_equal(SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)
		assert_that(proxy.replication_config.property_get_spawn(vpath)) \
				.is_equal(spawn)
		assert_that(proxy._properties.size()).is_equal(1)

	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"health", NodePath(":health"))
	proxy.register_property(&"health", NodePath(":health"))
	assert_that(proxy._properties.size()).is_equal(1)

	proxy.register_property(&"mp", NodePath(":mp"))
	proxy.finalize()
	assert_that(proxy._properties.size()).is_equal(2)
	assert_that(proxy.replication_config.get_properties().size()).is_equal(2)


func test_finalize_flow() -> void:
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
	assert_that(proxy._properties.has(&"health")).is_false()
	assert_that(proxy._deferred_node_props.size()).is_equal(1)

	proxy.finalize()

	assert_that(proxy._properties.has(&"health")).is_true()
	assert_that(proxy._deferred_node_props.size()).is_equal(0)
	var vpath := proxy._virtual_path(&"health")
	assert_that(proxy.replication_config.has_property(vpath)).is_true()

	var imported_proxy: StubProxy = auto_free(StubProxy.new())
	imported_proxy.name = "Proxy"
	imported_proxy.root_path = NodePath("..")
	root.add_child(imported_proxy)

	var real_path := NodePath(".:position")
	imported_proxy.register_property(&"pos", real_path)

	var imported := SceneReplicationConfig.new()
	imported.add_property(real_path)
	imported.property_set_replication_mode(
		real_path,
		SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
	)
	imported.property_set_spawn(real_path, true)
	imported.property_set_watch(real_path, false)
	imported_proxy.replication_config = imported

	imported_proxy.finalize()

	vpath = NodePath("Proxy:pos")
	assert_that(imported_proxy.replication_config.has_property(vpath)).is_true()
	assert_that(
		imported_proxy.replication_config.property_get_replication_mode(vpath),
	).is_equal(SceneReplicationConfig.REPLICATION_MODE_ALWAYS)
	assert_that(
		imported_proxy.replication_config.property_get_spawn(vpath),
	).is_true()
	assert_that(
		imported_proxy.replication_config.property_get_watch(vpath),
	).is_false()

	var stamp: StubStampedProxy = auto_free(StubStampedProxy.new())
	stamp.register_property(&"__tick", NodePath(""))
	stamp.finalize()
	assert_that(
		stamp.replication_config.has_property(stamp._virtual_path(&"__tick")),
	).is_true()


@warning_ignore("unused_parameter")
func test_dispatch_flow(
		fuzzer := Fuzzers.rangei(-1_000_000, 1_000_000),
		fuzzer_iterations := 20,
) -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"speed", NodePath(":speed"))

	assert_that(proxy._set(&"speed", 42)).is_true()
	assert_that(proxy._store.get(&"speed")).is_equal(42)
	assert_that(proxy._get(&"speed")).is_equal(42)

	assert_that(proxy._set(&"unknown", 42)).is_false()
	assert_that(proxy._get(&"unknown")).is_null()

	var raw: int = fuzzer.next_value()
	var value := Vector2(float(raw % 1000), floor(float(raw) / 1000.0))
	proxy.register_property(&"v", NodePath(":v"))

	assert_that(proxy._set(&"v", value)).is_true()
	assert_that(proxy._get(&"v")).is_equal(value)


func test_property_list_exposes_registered_names() -> void:
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
