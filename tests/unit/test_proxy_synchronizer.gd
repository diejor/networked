## Unit tests for [ProxySynchronizer] and [TickAwareSynchronizer].
##
## Both classes extend [MultiplayerSynchronizer] and rely on virtual methods
## [method ProxySynchronizer._read_property] and [method ProxySynchronizer._write_property].
## Tests use [StubProxy] and [StubTickAware] — inner subclasses that store values
## in a local [Dictionary] — so no scene tree or multiplayer API is required.
##
## [b]Coverage[/b]
## [ul]
## [li][method ProxySynchronizer.register_property] builds the correct [SceneReplicationConfig][/li]
## [li][method ProxySynchronizer.finalize] applies the config to [member MultiplayerSynchronizer.replication_config][/li]
## [li][method Object._get] / [method Object._set] dispatch through the virtual pair[/li]
## [li]Duplicate registrations are silently ignored[/li]
## [li][method TickAwareSynchronizer.finalize_with_tick] inserts [code]__tick[/code] first[/li]
## [li][code]__tick[/code] sets [member TickAwareSynchronizer._pending_tick][/li]
## [/ul]
class_name TestProxySynchronizer
extends NetworkedTestSuite


## [ProxySynchronizer] subclass that stores properties in memory instead of reading
## from the scene tree. Allows testing dispatch without a live node hierarchy.
@warning_ignore("missing_tool")
class StubProxy extends ProxySynchronizer:
	var _store: Dictionary[StringName, Variant] = {}

	func _read_property(_name: StringName, _path: NodePath) -> Variant:
		return _store.get(_name)

	func _write_property(_name: StringName, _path: NodePath, value: Variant) -> void:
		_store[_name] = value


## [TickAwareSynchronizer] subclass backed by the same in-memory store.
class StubTickAware extends TickAwareSynchronizer:
	var _store: Dictionary[StringName, Variant] = {}

	func _read_property(_name: StringName, _path: NodePath) -> Variant:
		return _store.get(_name)

	func _write_property(_name: StringName, _path: NodePath, value: Variant) -> void:
		_store[_name] = value


# ---------------------------------------------------------------------------
# ProxySynchronizer — register_property
# ---------------------------------------------------------------------------

func test_register_property_adds_to_internal_config() -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"health", NodePath(":health"))
	assert_that(proxy._config.has_property(NodePath(":health"))).is_true()


func test_register_property_default_mode_is_on_change() -> void:
	# Calling register_property with no explicit mode uses REPLICATION_MODE_ON_CHANGE.
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"velocity", NodePath(":velocity"))
	assert_that(
		proxy._config.property_get_replication_mode(NodePath(":velocity"))
	).is_equal(SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)


func test_register_property_spawn_and_mode_are_independent() -> void:
	# Verify that spawn=true can be set independently of the replication mode.
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"hp", NodePath(":hp"), SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE, true)
	assert_that(proxy._config.property_get_spawn(NodePath(":hp"))).is_true()
	assert_that(
		proxy._config.property_get_replication_mode(NodePath(":hp"))
	).is_equal(SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)


func test_register_property_sets_spawn_flag() -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"pos", NodePath(":position"), SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE, true)
	assert_that(proxy._config.property_get_spawn(NodePath(":pos"))).is_true()


func test_register_property_duplicate_is_ignored() -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"health", NodePath(":health"))
	proxy.register_property(&"health", NodePath(":health"))
	# Only one entry in _properties.
	assert_that(proxy._properties.size()).is_equal(1)


func test_register_multiple_properties_all_present() -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"hp", NodePath(":hp"))
	proxy.register_property(&"mp", NodePath(":mp"))
	assert_that(proxy._properties.size()).is_equal(2)
	assert_that(proxy._config.get_properties().size()).is_equal(2)


# ---------------------------------------------------------------------------
# ProxySynchronizer — finalize
# ---------------------------------------------------------------------------

func test_finalize_applies_config_to_replication_config() -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"score", NodePath(":score"))
	proxy.finalize()
	assert_that(proxy.replication_config).is_not_null()
	assert_that(proxy.replication_config.has_property(NodePath(":score"))).is_true()


# ---------------------------------------------------------------------------
# ProxySynchronizer — _get / _set dispatch
# ---------------------------------------------------------------------------

func test_set_registered_property_calls_write_property() -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"speed", NodePath(":speed"))
	proxy._set(&"speed", 42)
	assert_that(proxy._store.get(&"speed")).is_equal(42)


func test_set_registered_property_returns_true() -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"speed", NodePath(":speed"))
	assert_that(proxy._set(&"speed", 0)).is_true()


func test_set_unregistered_property_returns_false() -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	assert_that(proxy._set(&"unknown", 99)).is_false()


func test_get_registered_property_calls_read_property() -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"level", NodePath(":level"))
	proxy._store[&"level"] = 7
	assert_that(proxy._get(&"level")).is_equal(7)


func test_get_unregistered_property_returns_null() -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	assert_that(proxy._get(&"unknown")).is_null()


func test_roundtrip_set_then_get() -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"pos", NodePath(":pos"))
	proxy._set(&"pos", Vector2(1.0, 2.0))
	assert_that(proxy._get(&"pos")).is_equal(Vector2(1.0, 2.0))


# ---------------------------------------------------------------------------
# ProxySynchronizer — _get_property_list
# ---------------------------------------------------------------------------

func test_get_property_list_contains_registered_names() -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"alpha", NodePath(":alpha"))
	proxy._store[&"alpha"] = 1.0

	var names: Array[StringName] = []
	for entry in proxy._get_property_list():
		names.append(StringName(entry["name"]))
	assert_that(names.has(&"alpha")).is_true()


func test_get_property_list_length_matches_registered_count() -> void:
	var proxy: StubProxy = auto_free(StubProxy.new())
	proxy.register_property(&"a", NodePath(":a"))
	proxy.register_property(&"b", NodePath(":b"))
	proxy._store[&"a"] = 0
	proxy._store[&"b"] = 0
	assert_that(proxy._get_property_list().size()).is_equal(2)


# ---------------------------------------------------------------------------
# TickAwareSynchronizer — finalize_with_tick
# ---------------------------------------------------------------------------

func test_finalize_with_tick_inserts_tick_as_first_property() -> void:
	var sync: StubTickAware = auto_free(StubTickAware.new())
	sync.register_property(&"hp", NodePath(":hp"))
	sync.finalize_with_tick()

	var props := sync.replication_config.get_properties()
	assert_that(props.size()).is_equal(2)
	assert_that(String(props[0])).is_equal(":__tick")


func test_finalize_with_tick_registered_property_follows_tick() -> void:
	var sync: StubTickAware = auto_free(StubTickAware.new())
	sync.register_property(&"speed", NodePath(":speed"))
	sync.finalize_with_tick()

	var props := sync.replication_config.get_properties()
	assert_that(String(props[1])).is_equal(":speed")


func test_finalize_with_tick_tick_mode_is_always() -> void:
	var sync: StubTickAware = auto_free(StubTickAware.new())
	sync.finalize_with_tick()

	var mode := sync.replication_config.property_get_replication_mode(NodePath(":__tick"))
	assert_that(mode).is_equal(SceneReplicationConfig.REPLICATION_MODE_ALWAYS)


func test_finalize_with_tick_tick_spawn_is_false() -> void:
	var sync: StubTickAware = auto_free(StubTickAware.new())
	sync.finalize_with_tick()
	assert_that(sync.replication_config.property_get_spawn(NodePath(":__tick"))).is_false()


# ---------------------------------------------------------------------------
# TickAwareSynchronizer — __tick _set
# ---------------------------------------------------------------------------

func test_set_tick_updates_pending_tick() -> void:
	var sync: StubTickAware = auto_free(StubTickAware.new())
	sync._set(&"__tick", 42)
	assert_that(sync._pending_tick).is_equal(42)


func test_set_tick_returns_true() -> void:
	var sync: StubTickAware = auto_free(StubTickAware.new())
	assert_that(sync._set(&"__tick", 0)).is_true()


func test_set_non_tick_delegates_to_super_write_property() -> void:
	var sync: StubTickAware = auto_free(StubTickAware.new())
	sync.register_property(&"pos", NodePath(":pos"))
	sync._set(&"pos", Vector3.ONE)
	assert_that(sync._store.get(&"pos")).is_equal(Vector3.ONE)


func test_set_tick_does_not_pollute_store() -> void:
	var sync: StubTickAware = auto_free(StubTickAware.new())
	sync._set(&"__tick", 7)
	assert_that(sync._store.has(&"__tick")).is_false()
