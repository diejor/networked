## Embedding-conformance suite skeleton — the SAME scenarios under BOTH providers.
##
## Each scenario is written once against [NetwEmbeddingWorld] and run twice, once
## for [NetwScopedWorld] (subpath [MultiplayerTree], the harness topology) and
## once for [NetwRootWorld] (root-installed session, the shipping topology). A
## paired-method-per-scenario shape keeps failures naming the provider that broke
## and lets the two run serialized (the root case mutates the [SceneTree] default,
## so it is captured and restored per case).
##
## This is the skeleton the contract slices land against: it exists BEFORE the
## phase machine so the resolve-ordering change is asserted under both providers
## rather than discovered live (see
## [code].agents/design/netw-embedding-contract-design.md[/code] §8.1, §3.1). Grow
## the matrix here — scene declare/activate/change, config survives scene swap,
## sync reaches every admitted peer, despawn, teardown — one scenario at a time.
class_name TestEmbeddingConformance
extends NetwTestSuite

const _LEVEL := preload("res://tests/conformance/probe.tscn")

var _original: MultiplayerAPI
var _active_root: NetwRootWorld


func before_test() -> void:
	_original = get_tree().get_multiplayer()


func after_test() -> void:
	if _active_root != null:
		_active_root.dispose()
		_active_root = null
	get_tree().set_multiplayer(_original)
	await drain_frames(get_tree(), 3)
	super.after_test()

#region Provider builders

func _scoped() -> NetwScopedWorld:
	return NetwScopedWorld.new(make_harness())


func _root() -> NetwRootWorld:
	_active_root = NetwRootWorld.new(self)
	return _active_root

#endregion

#region Scenarios (provider-agnostic — never name a MultiplayerTree or install verb)

# The host comes online through the session's own host verb and identifies as the
# server at peer 1, in any embedding.
func _scenario_host_comes_online(world: NetwEmbeddingWorld) -> void:
	var host := await world.host()
	assert_bool(host.is_online).override_failure_message(
		"[%s] host never came online through its own verb" % world.provider(),
	).is_true()
	assert_bool(host.is_host).override_failure_message(
		"[%s] online host does not report is_host" % world.provider(),
	).is_true()
	assert_int(host.get_unique_id()).override_failure_message(
		"[%s] host is not peer 1" % world.provider(),
	).is_equal(1)


# A joining client is admitted to its own session and appears in the host roster,
# the replication substrate crossing the embedding boundary.
func _scenario_client_is_admitted(world: NetwEmbeddingWorld) -> void:
	var host := await world.host()
	var client := await world.add_client("p1")

	assert_object(client.local_participant).override_failure_message(
		"[%s] client was never admitted to its own session" % world.provider(),
	).is_not_null()

	var client_id := client.get_unique_id()
	var seen := await world.pump_until(
		func() -> bool: return host.peer_get_participant(client_id) != null
	)
	assert_bool(seen).override_failure_message(
		"[%s] host admitted no roster row for the client" % world.provider(),
	).is_true()


# The roster crosses both ways between two clients: the host sees both, and each
# client's roster (its own accept plus the other's relayed accept/backfill)
# includes the other client. Asserted client-to-client so it does not depend on
# whether the host carries a local player — a host-role config difference
# (dedicated vs listen), not an embedding difference, deliberately not asserted
# here.
func _scenario_roster_crosses_both_ways(world: NetwEmbeddingWorld) -> void:
	var host := await world.host()
	var c1 := await world.add_client("p1")
	var c2 := await world.add_client("p2")
	var id1 := c1.get_unique_id()
	var id2 := c2.get_unique_id()

	var host_sees_both := await world.pump_until(
		func() -> bool:
			return host.peer_get_participant(id1) != null \
					and host.peer_get_participant(id2) != null
	)
	assert_bool(host_sees_both).override_failure_message(
		"[%s] host roster is missing one of the two clients" % world.provider(),
	).is_true()

	var clients_see_each_other := await world.pump_until(
		func() -> bool:
			return c1.peer_get_participant(id2) != null \
					and c2.peer_get_participant(id1) != null
	)
	assert_bool(clients_see_each_other).override_failure_message(
		"[%s] client rosters did not cross (each should see the other)"
		% world.provider(),
	).is_true()


# A host-spawned replicated entity reaches every admitted peer's liveness, the
# replication substrate crossing the embedding boundary. This is the cell where a
# "remote content private to the host" regression (the residual the design names)
# gets a failing test under whichever provider breaks.
func _scenario_spawn_reaches_every_peer(world: NetwEmbeddingWorld) -> void:
	var _host := await world.host()
	var c1 := await world.add_client("p1")
	var c2 := await world.add_client("p2")

	var route := await world.spawn_probe("probe")

	var c1_live := await world.pump_until(
		func() -> bool:
			return c1._liveness.route_state(route) \
					== LivenessShell.State.LIVE
	)
	assert_bool(c1_live).override_failure_message(
		"[%s] host spawn never reached client 1" % world.provider(),
	).is_true()

	var c2_live := await world.pump_until(
		func() -> bool:
			return c2._liveness.route_state(route) \
					== LivenessShell.State.LIVE
	)
	assert_bool(c2_live).override_failure_message(
		"[%s] host spawn never reached client 2" % world.provider(),
	).is_true()


# A scene declared through the provider's own native path comes online on the
# host once it is up, in any embedding. This is the bootstrap-ordering cell the
# phase machine owns: the declaration lands before the session settles, and the
# settle step brings the initial scene online the same way under both providers.
func _scenario_declared_scene_comes_online(world: NetwEmbeddingWorld) -> void:
	world.declare_initial_scene(_LEVEL)
	var host := await world.host()

	var online := await world.pump_until(
		func() -> bool: return not host.scene_instances().is_empty()
	)
	assert_bool(online).override_failure_message(
		"[%s] declared initial scene never came online on the host"
		% world.provider(),
	).is_true()


# A NetwService node mounted under the host configures the session, in any
# embedding. The service resolves its session through the branch NetwMultiplayer,
# so a root install with no owning tree must run the same _service_entered
# lifecycle a scoped tree does. Before that lifecycle resolved the api, a
# root-installed clock never configured and this cell went red under root only.
func _scenario_service_configures_on_mount(world: NetwEmbeddingWorld) -> void:
	var host := await world.host()
	world.mount_clock()

	var configured := await world.pump_until(
		func() -> bool: return host.clock.is_configured()
	)
	assert_bool(configured).override_failure_message(
		"[%s] a clock service mounted under the host never configured the session"
		% world.provider(),
	).is_true()

#endregion

#region Conformance cases (each scenario × each provider)

func test_host_comes_online_scoped() -> void:
	await _scenario_host_comes_online(_scoped())


func test_host_comes_online_root() -> void:
	await _scenario_host_comes_online(_root())


func test_client_is_admitted_scoped() -> void:
	await _scenario_client_is_admitted(_scoped())


func test_client_is_admitted_root() -> void:
	await _scenario_client_is_admitted(_root())


func test_roster_crosses_both_ways_scoped() -> void:
	await _scenario_roster_crosses_both_ways(_scoped())


func test_roster_crosses_both_ways_root() -> void:
	await _scenario_roster_crosses_both_ways(_root())


func test_spawn_reaches_every_peer_scoped() -> void:
	await _scenario_spawn_reaches_every_peer(_scoped())


func test_spawn_reaches_every_peer_root() -> void:
	await _scenario_spawn_reaches_every_peer(_root())


func test_declared_scene_comes_online_scoped() -> void:
	await _scenario_declared_scene_comes_online(_scoped())


func test_declared_scene_comes_online_root() -> void:
	await _scenario_declared_scene_comes_online(_root())


func test_service_configures_on_mount_scoped() -> void:
	await _scenario_service_configures_on_mount(_scoped())


func test_service_configures_on_mount_root() -> void:
	await _scenario_service_configures_on_mount(_root())

#endregion
