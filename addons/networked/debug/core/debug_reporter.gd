## Game-side debug telemetry reporter for the Networked debugger plugin.
##
## This is a singleton (Autoload) node that collects telemetry from all active
## [MultiplayerTree] instances in the process and forwards them to the editor.
## [br][br]
## The bootstrap only creates this node when reporter policy allows it.
extends Node

class_name DebugReporter

## Emitted for every [NetwTreeEvent] a [TreeProbe] dispatches.
##
## The validator registry ([method _route_tree_event]) subscribes here and fans
## each event out to every installed [NetwValidator] whose [method
## NetwValidator.interests] matches [member NetwTreeEvent.kind].
signal tree_event(event: NetwTreeEvent)

static var _reporting_enabled: bool = false
static var _reporting_checked: bool = false
static var _capture_registered: bool = false


## Explicitly enables or disables debug reporting.
## [br][br]
## If [param enabled] is [code]true[/code], the reporter attempts to register
## a message capture with the [EngineDebugger].
static func set_enabled(enabled: bool) -> void:
	_reporting_enabled = enabled
	_reporting_checked = true
	var reporter := _get_instance()
	if reporter:
		if enabled:
			reporter._try_register_capture()
		else:
			reporter._unregister_capture()

## Property-style access for the singleton instance.
var enabled: bool:
	get:
		return _debug_build()
	set(value):
		set_enabled(value)

## Unique ID for this reporter instance; used to deduplicate echos in the editor.
var reporter_id: String = ""

var _trees: Array[MultiplayerTree] = []

# Trees that have completed the online finalize upgrade (monitors registered,
# live-role wire registration emitted). Distinct from [member _trees], which
# also holds offline registrants.
var _finalized: Array[MultiplayerTree] = []

var _debug_contexts: Dictionary = { }

# Windows hosting debugger-spawned [MultiplayerTree] clones. Freed in
# [method reset_state] so a game-stop or tab-close tears down only the
# debugger's own trees, never the app's.
var _spawned_windows: Array[Window] = []

# Clean configuration templates keyed by tree_name, captured offline in
# [method register_tree] before the tree connects or its [TreeProbe] attaches.
# [method _handle_spawn_tree] clones these, never the live tree, which carries
# the probe and runtime-spawned scenes that break [method Node.duplicate].
var _templates: Dictionary = { }

var _message_queue: Array = []
var _flush_pending: bool = false

var _auto_break: bool = false

var _watchdog: ErrorWatchdog
var _telemetry: NetwTelemetryBuffer
var _cycle_peer_events: Array = []
var _validators: Array[NetwValidator] = []

# Per-trigger manifest emission caps. Findings above these rates are dropped.
const _MANIFEST_MAX_PER_SEC: int = 3
const _MANIFEST_MAX_PER_MIN: int = 10

var _manifest_count_sec: Dictionary = { }
var _manifest_count_min: Dictionary = { }
var _last_manifest_sec_msec: Dictionary = { }
var _last_manifest_min_msec: Dictionary = { }

# Callables invoked with each [NetwFinding] the pipeline emits. Registered via
# [code]Netw.dbg.on_violation[/code].
var _violation_listeners: Array[Callable] = []

var _is_sending_manifest: bool = false
var _clock_monitor: MultiplayerClockMonitor = null
var _lagcomp_monitor: LagCompensationMonitor = null
var _interest_monitor: InterestMonitor = null
var _relay_monitor: EntityRelayMonitor = null
var _trace_sink: Callable
var _window_pin: WindowPin

var _dbg: NetwHandle = Netw.dbg.handle(self)


## Resets all debug registries, history, and telemetry.
## [br][br]
## This performs a deep reset, freeing all internal [TreeProbe]
## instances and clearing the [NetwTrace] history.
func reset_state() -> void:
	_unregister_capture()
	_reporting_checked = false
	_reporting_enabled = false

	if _clock_monitor:
		_clock_monitor.clear_all()
	if _relay_monitor:
		_relay_monitor.clear_all()

	for w: Window in _spawned_windows:
		if is_instance_valid(w):
			w.queue_free()
	_spawned_windows.clear()

	for t: Node in _templates.values():
		if is_instance_valid(t):
			t.free()
	_templates.clear()

	for ctx in _debug_contexts.values():
		if is_instance_valid(ctx):
			ctx.free()

	_debug_contexts.clear()
	_trees.clear()
	_finalized.clear()
	_violation_listeners.clear()
	_validators.clear()
	_install_builtin_validators()

	_message_queue.clear()
	_cycle_peer_events.clear()
	active_visualizers.clear()

	if _telemetry:
		_telemetry.clear()

	if LocalLoopbackSession.shared:
		LocalLoopbackSession.shared.reset()
		LocalLoopbackSession.shared = null

	Netw.dbg.reset()
	_dbg.trace("Reporter: State reset (deep).")


static func _get_instance() -> DebugReporter:
	return Netw.dbg.get_reporter() as DebugReporter


func _try_register_capture() -> void:
	if _has_local_session() and not _capture_registered:
		EngineDebugger.register_message_capture(
			"networked",
			func(message: String, data: Array) -> bool:
				_on_editor_message(message, data)
				return true
		)
		_capture_registered = true


func _unregister_capture() -> void:
	if not _capture_registered:
		return
	_capture_registered = false
	if EngineDebugger.has_method("unregister_message_capture"):
		EngineDebugger.unregister_message_capture("networked")


func _init() -> void:
	randomize()
	reporter_id = "%08x" % (randi() % 0xFFFFFFFF)


func _enter_tree() -> void:
	Netw.dbg.register_reporter(self)
	if not _debug_build():
		return
	Netw.dbg._set_enabled(true)

	Netw.dbg.reset()
	_trace_sink = func(
			msg: String,
			payload: Dictionary,
			mt: MultiplayerTree = null,
	) -> void:
		_queue(msg, payload, mt)
	Netw.dbg.install_trace_sink(_trace_sink)

	_try_register_capture()

	var cap: int = ProjectSettings.get_setting(
		"debug/networked/telemetry_buffer_size",
		120,
	)
	_telemetry = NetwTelemetryBuffer.new(cap)
	_install_builtin_validators()
	tree_event.connect(_route_tree_event)

	_clock_monitor = MultiplayerClockMonitor.new()
	_clock_monitor.name = "MultiplayerClockMonitor"
	add_child(_clock_monitor)

	if ProjectSettings.get_setting("debug/networked/performance_monitors", true):
		_lagcomp_monitor = LagCompensationMonitor.new()
		_lagcomp_monitor.name = "LagCompensationMonitor"
		add_child(_lagcomp_monitor)
		_interest_monitor = InterestMonitor.new()
		_interest_monitor.name = "InterestMonitor"
		add_child(_interest_monitor)
		_relay_monitor = EntityRelayMonitor.new()
		_relay_monitor.name = "EntityRelayMonitor"
		add_child(_relay_monitor)

	_watchdog = ErrorWatchdog.new()
	add_child(_watchdog)
	_watchdog.cpp_error_caught.connect(_on_cpp_error_caught)

	_window_pin = WindowPin.new()
	_window_pin.name = "WindowPin"
	_window_pin.geometry_reported.connect(_on_window_geometry_reported)
	add_child(_window_pin)


func _exit_tree() -> void:
	_unregister_capture()

	if not _debug_build():
		Netw.dbg.unregister_reporter(self)
		return

	Netw.dbg.clear_trace_sink(_trace_sink)
	_trace_sink = Callable()
	Netw.dbg._set_enabled(false)

	for mt: MultiplayerTree in _trees:
		var event := NetwSessionEvent.new()
		event.tree_name = mt.get_tree_name()
		_queue("networked:session_unregistered", event.to_dict(), mt)

	_flush_now()
	Netw.dbg.unregister_reporter(self)


## Registers a [MultiplayerTree] for debug reporting.
## [br][br]
## Offline phase of two-phase registration: creates the [TreeProbe] so the
## tree is observed from [code]_enter_tree[/code] at
## [constant NetwSessionInterface.Role.NONE], and emits the first wire session
## registration at that role so the editor's peer registry can show the tree
## before it connects. Registers no performance monitors yet. The online
## upgrade happens in [method finalize_tree].
func register_tree(mt: MultiplayerTree) -> void:
	if not _debug_build() or mt in _trees:
		return

	var tree_name := mt.get_tree_name()
	_dbg.info(
		"Reporter: [Register] '%s' (role=%s, local_editor=%s)" % [
			tree_name,
			_role_name(mt),
			_has_local_session(),
		],
	)

	_trees.append(mt)

	# Snapshot a clean template while the tree is still offline (no runtime
	# scenes) and before the probe below attaches. Duplicating the live tree
	# later would drag in the probe and runtime-spawned nodes.
	# Only an attached editor can trigger a spawn, so skip the cost otherwise.
	if _has_local_session() and not _templates.has(tree_name):
		var template := mt.duplicate() as MultiplayerTree
		if template != null:
			_templates[tree_name] = template

	var ctx := TreeProbe.new(mt, self)
	ctx.name = "TreeProbe"
	ctx.clock_pong_captured.connect(
		func(d: Dictionary) -> void:
			if _clock_monitor:
				_clock_monitor.update_local_clock(mt, d)
			var sample := MultiplayerClockSample.from_dict(d, mt.get_tree_name())
			_queue("networked:clock_sample", sample.to_dict(), mt)
	)
	mt.add_child(ctx)
	_debug_contexts[mt] = ctx

	report_session_registered(mt)


## Upgrades an offline-registered tree to a live session. Idempotent.
## [br][br]
## Online phase of two-phase registration: registers the performance monitors
## and re-emits the wire session registration with the now-live role and
## peer id, upgrading the offline row [method register_tree] already created.
## Falls back to [method register_tree] when the tree was never registered
## offline (e.g. the reporter came online after the tree's
## [code]_enter_tree[/code]), so behavior is identical whether or not the
## offline phase ran.
func finalize_tree(mt: MultiplayerTree) -> void:
	if not _debug_build():
		return
	if mt not in _trees:
		register_tree(mt)
	if mt in _finalized:
		return
	_finalized.append(mt)

	if _lagcomp_monitor:
		_lagcomp_monitor.register_tree(mt)
	if _interest_monitor:
		_interest_monitor.register_tree(mt)
	if _relay_monitor:
		_relay_monitor.register_tree(mt)

	report_session_registered(mt)


## Dispatches a [NetwTreeEvent] from a [TreeProbe] onto [signal tree_event],
## which the validator registry routes to matching validators.
func dispatch_tree_event(event: NetwTreeEvent) -> void:
	tree_event.emit(event)


# Installs the validators the debugger ships with, through the same public path
# a project would use ([method install_validator]).
func _install_builtin_validators() -> void:
	install_validator(SimplifyPathRaceValidator.new())
	install_validator(ZombieValidator.new())
	install_validator(TopologyNetValidator.new())
	install_validator(TeleportProbe.new())


## Registers [param v] to receive matching [NetwTreeEvent]s. Dev/test/setup only.
func install_validator(v: NetwValidator) -> void:
	if v and v not in _validators:
		_validators.append(v)


## Removes a previously installed [param v].
func remove_validator(v: NetwValidator) -> void:
	_validators.erase(v)


# Routes one [param event] to every installed validator whose
# [method NetwValidator.interests] matches, in ascending [member
# NetwValidator.priority] order. Each validator receives a fresh [NetwReport]
# anchored at the event's tree, probe, and the operation span the [TreeProbe]
# threaded through [code]event.data["span"][/code] (the probe ends it after
# routing, so a validator may fail it via [method NetwReport.fail]).
func _route_tree_event(event: NetwTreeEvent) -> void:
	if _validators.is_empty():
		return

	var mt := event.tree
	if not is_instance_valid(mt):
		return

	var matched: Array[NetwValidator] = []
	for v: NetwValidator in _validators:
		if event.kind in v.interests():
			matched.append(v)
	if matched.is_empty():
		return

	matched.sort_custom(
		func(a: NetwValidator, b: NetwValidator) -> bool:
			return a.priority < b.priority
	)

	var op_span := event.data.get("span") as NetwSpan
	var probe := _debug_contexts.get(mt) as TreeProbe
	for v: NetwValidator in matched:
		var report := NetwReport.new(self, mt, probe, op_span)
		v.inspect(event, report)


## Emits a session registration event for the given [param mt].
## [br][br]
## Called by [method register_tree] and [method finalize_tree] at each phase
## of two-phase registration, and by [TreeProbe] when the tree's authority
## role changes.
func report_session_registered(mt: MultiplayerTree) -> void:
	if not is_instance_valid(mt):
		return

	var backend_class := String(mt.scheme)

	var event := NetwSessionEvent.new()
	event.tree_name = mt.get_tree_name()
	event.username = (
			NetwIdentity.username_of(mt.local_player.owner)
			if mt.local_player else ""
	)
	event.role = mt.role
	event.is_server = (
			mt.is_host if mt.role != NetwSessionInterface.Role.NONE else false
	)
	event.backend_class = backend_class
	event.rid = reporter_id
	# get_unique_id() returns 1 for a default MultiplayerAPI with no peer, so an
	# offline tree would look like "peer 1, online". Gate both on a live session.
	var is_online := mt.state == NetwSessionInterface.State.ONLINE
	event.online = is_online
	event.peer_id = mt.multiplayer_api.get_unique_id() if \
	(is_online and mt.multiplayer_api) else 0
	event.spawned = Netw.dbg.is_debug_tree(mt)
	_queue("networked:session_registered", event.to_dict(), mt)


## Unregisters a [MultiplayerTree] from debug reporting.
func unregister_tree(mt: MultiplayerTree) -> void:
	if mt not in _trees:
		return

	_trees.erase(mt)
	_finalized.erase(mt)
	if _lagcomp_monitor:
		_lagcomp_monitor.unregister_tree(mt)
	if _interest_monitor:
		_interest_monitor.unregister_tree(mt)
	if _relay_monitor:
		_relay_monitor.unregister_tree(mt)
	var ctx: TreeProbe = _debug_contexts.get(mt)
	if is_instance_valid(ctx):
		ctx.free()
	_debug_contexts.erase(mt)

	var tree_name := mt.get_tree_name()

	var event := NetwSessionEvent.new()
	event.tree_name = tree_name
	event.rid = reporter_id
	event.peer_id = mt.multiplayer_api.get_unique_id() if \
	mt.multiplayer_api else 0
	_queue("networked:session_unregistered", event.to_dict(), mt)
	_flush_now()

#region --- Signal Handlers ----------------------------------------------------

var _sending_manifest: bool = false


func _on_cpp_error_caught(timestamp: int, error_text: String) -> void:
	if not _debug_build() or _sending_manifest:
		return

	_sending_manifest = true
	var active := Netw.dbg.active_span()

	var mt: MultiplayerTree = null
	if active and active.get("_mt") is WeakRef:
		mt = (active.get("_mt") as WeakRef).get_ref() as MultiplayerTree

	if not is_instance_valid(mt):
		if not _trees.is_empty():
			_dbg.debug(
				"Reporter: [CppError] no active span tree context - " +
				"attributing to first tree",
				func(m): push_warning(m)
			)
			mt = _trees[0]
		else:
			_dbg.debug(
				"Reporter: [CppError] no active span and no trees - dropping",
				func(m): push_warning(m)
			)
			_sending_manifest = false
			return

	var m := NetwCppErrorManifest.new()
	m.trigger = "CPP_ERROR_LOG_WATCHDOG"
	m.timestamp_usec = timestamp
	m.errors = [error_text]
	m.error_text = error_text

	if is_instance_valid(mt) and mt in _debug_contexts:
		var ctx := _debug_contexts[mt] as TreeProbe
		m.node_snapshot = ctx.build_crash_snapshot(active)

	_emit_finding(NetwFinding.new(m, "CPP_ERROR_LOG_WATCHDOG", active), mt)
	_sending_manifest = false

#endregion


# Returns the [code]tree_name[/code] for the current context.
func _active_tree_name(active_span: RefCounted = null) -> String:
	if active_span:
		var tn: Variant = active_span.get("tree_name")
		if tn is String and not (tn as String).is_empty():
			return tn as String

	if not _trees.is_empty():
		var mt: MultiplayerTree = _trees[0]
		return mt.get_tree_name()

	return ""


# Queues the peer-connect wire event and, on host authority, opens the
# [code]peer_connect[/code] operation span for the [TreeProbe] to thread through
# dispatch. Detection now lives in [SimplifyPathRaceValidator]; this only builds
# the span and returns it. The probe ends it after routing.
func _on_peer_connected(peer_id: int, mt: MultiplayerTree) -> NetwPeerSpan:
	var tree_name := mt.get_tree_name()
	var ev := {
		"tree_name": tree_name,
		"peer_id": peer_id,
		"event": "connected",
	}
	_cycle_peer_events.append(ev)

	var event := NetwPeerEvent.new()
	event.tree_name = tree_name
	event.peer_id = peer_id
	_queue("networked:peer_connected", event.to_dict(), mt)

	if mt.is_host:
		var span: NetwPeerSpan = Netw.dbg.peer_span(
			mt,
			"peer_connect",
			[peer_id],
			{ "tree": tree_name },
		)
		span.step("server_received_connect")
		return span
	return null


func _on_peer_disconnected(peer_id: int, mt: MultiplayerTree) -> void:
	var tree_name := mt.get_tree_name()
	var ev := {
		"tree_name": tree_name,
		"peer_id": peer_id,
		"event": "disconnected",
	}
	_cycle_peer_events.append(ev)

	var event := NetwPeerEvent.new()
	event.tree_name = tree_name
	event.peer_id = peer_id
	_queue("networked:peer_disconnected", event.to_dict(), mt)

	# The delayed zombie scan now lives in ZombieValidator, driven off the
	# PEER_DISCONNECTED event the probe dispatches.


# Queues the scene-spawn wire event and, on host authority, opens the
# [code]scene_spawn[/code] operation span for the [TreeProbe] to thread through
# dispatch. Detection now lives in [SimplifyPathRaceValidator]; this only builds
# the span and returns it. The probe derives the causal token and ends it.
func _on_scene_spawned_logic(
		scene: MultiplayerScene,
		mt: MultiplayerTree,
) -> NetwPeerSpan:
	var tree_name := mt.get_tree_name()
	var event := NetwSceneEvent.new()
	event.tree_name = tree_name
	event.event = "spawned"
	event.scene_name = str(scene.level.name)
	_queue("networked:scene_event", event.to_dict(), mt)

	if not mt.is_host:
		return null

	var peers: Array = []
	if mt.multiplayer_api:
		peers = mt.multiplayer_api.get_peers()

	var scene_span := Netw.dbg.peer_span(
		mt,
		"scene_spawn",
		peers,
		{ "scene_name": str(scene.level.name), "tree": tree_name },
	)
	scene_span.step("spawners_registering")
	return scene_span


# On host authority, opens the [code]player_spawn[/code] operation span for the
# [TreeProbe] to thread through dispatch. Detection
# ([SimplifyPathRaceValidator], [TopologyNetValidator]) runs off the dispatched
# event and the topology snapshot is emitted by the probe; this only builds the
# span and returns it. The probe ends it after routing.
func _on_player_spawned_logic(
		player: Node,
		mt: MultiplayerTree,
		scene_token: CheckpointToken,
) -> NetwSpan:
	var tree_name := mt.get_tree_name()
	if not mt.is_host:
		return null

	var peers: Array = []
	if mt.multiplayer_api:
		peers = Array(mt.multiplayer_api.get_peers())

	var spawn_span := Netw.dbg.peer_span(
		mt,
		"player_spawn",
		peers,
		{
			"player": player.name,
			"tree": tree_name,
		},
		scene_token,
	).with_node(player)
	spawn_span.step("player_entered_tree")
	return spawn_span


# Called by [TreeProbe] when a scene despawns.
func _on_scene_despawned_logic(scene: MultiplayerScene, mt: MultiplayerTree) -> void:
	var tree_name := mt.get_tree_name()
	var event := NetwSceneEvent.new()
	event.tree_name = tree_name
	event.event = "despawned"
	event.scene_name = str(scene.level.name)
	_queue("networked:scene_event", event.to_dict(), mt)

# --- Snapshot Protocol --------------------------------------------------------


# Re-emits current state for a late-joining editor.
func _on_request_snapshot() -> void:
	if not _debug_build():
		return
	_emit_current_state()


# Emits session and topology snapshots for all active trees.
func _emit_current_state() -> void:
	for mt: MultiplayerTree in _trees:
		var tree_name := mt.get_tree_name()
		var backend_class := String(mt.scheme)

		var event := NetwSessionEvent.new()
		event.tree_name = tree_name
		event.username = (
				NetwIdentity.username_of(mt.local_player.owner)
				if mt.local_player else ""
		)
		event.role = mt.role
		event.is_server = (
				mt.is_host if mt.role != NetwSessionInterface.Role.NONE else false
		)
		event.backend_class = backend_class
		event.rid = reporter_id
		event.peer_id = mt.multiplayer_api.get_unique_id() if \
		mt.multiplayer_api else 0
		_queue("networked:session_registered", event.to_dict(), mt)

		var ctx := _debug_contexts.get(mt) as TreeProbe
		if not is_instance_valid(ctx):
			continue

		if mt.role != NetwSessionInterface.Role.NONE and mt.is_host:
			# Server sends topology for all active players.
			for player in mt.get_all_players():
				ctx.send_topology_snapshot(player.owner)
		elif ctx.local_player != null:
			# Client only sends its own.
			ctx.send_topology_snapshot(ctx.local_player.owner)


# Formats Godot's raw [code]error[/code] debugger message.
func _format_cpp_error(data: Array) -> String:
	var parts: PackedStringArray = []
	var prefix := "WARNING" if data.size() > 5 and data[5] else "ERROR"
	var code: String = str(data[3]) if data.size() > 3 else ""
	var descr: String = str(data[4]) if data.size() > 4 else ""
	var func_: String = str(data[0]) if data.size() > 0 else ""
	var file_: String = str(data[1]) if data.size() > 1 else ""
	var line_: int = int(data[2]) if data.size() > 2 else 0

	if not code.is_empty():
		parts.append("%s: %s" % [prefix, code])
	if not descr.is_empty():
		parts.append("  %s" % descr)
	if not func_.is_empty():
		parts.append("  at: %s (%s:%d)" % [func_, file_, line_])

	return "\n".join(parts) if not parts.is_empty() else str(data)


static var active_visualizers: Dictionary = { }


static func is_visualizer_enabled(_viz_name: String) -> bool:
	return false


static func get_peer_debug_color(peer_id: int) -> Color:
	var phi_conj := 0.618033988749895
	var hue := fmod(float(abs(peer_id)) * phi_conj, 1.0)
	return Color.from_hsv(hue, 0.6, 0.9)

# --- Incoming Editor Messages -------------------------------------------------


func _on_editor_message(message: String, data: Array) -> void:
	if data.is_empty():
		_dbg.warn(
			"Reporter: [EditorMessage] '%s' received with empty " +
			"payload." % [message],
			func(m: String) -> void: push_warning(m)
		)
		return

	match message:
		"pin_window", "networked:pin_window":
			_dbg.trace("Reporter: [PinWindow] payload=%s", [str(data)])
			if _window_pin and data.size() >= 1 and data[0] is Dictionary:
				var d: Dictionary = data[0]
				_window_pin.pin(d.get("rect", null))
		"unpin_window", "networked:unpin_window":
			_dbg.trace("Reporter: [UnpinWindow]")
			if _window_pin:
				_window_pin.unpin()
		"watch_node":
			_route_watch(data[0], true)
		"unwatch_node":
			_route_watch(data[0], false)
		"spawn_tree":
			_handle_spawn_tree(data[0])
		"toggle_embed":
			_handle_toggle_embed(data[0])
		"set_auto_break":
			_auto_break = true if data[0] else false
		"cpp_error_caught":
			_on_cpp_error_caught(Time.get_ticks_usec(), _format_cpp_error(data))
		"request_snapshot":
			_on_request_snapshot()
		"inspect_node":
			_handle_inspect_node(data[0])
		"visualizer_toggle":
			_handle_visualizer_toggle(data[0])
		"remote_clock_sample":
			if _clock_monitor:
				_clock_monitor.update_relayed_clock(NetwEnvelope.from_dict(data[0]))
		"remote_session_unregistered":
			if _clock_monitor:
				_clock_monitor.remove_relayed_clock(NetwEnvelope.from_dict(data[0]))


# Routes an editor watch/unwatch command to the [TreeProbe] that owns the named
# tree. The probe owns the demand-driven replication watch state.
func _route_watch(d: Dictionary, watch: bool) -> void:
	var tree_name: String = d.get("tree_name", "")
	var np := NodePath(d.get("node_path", ""))
	for mt: MultiplayerTree in _trees:
		if mt.get_tree_name() != tree_name:
			continue
		var probe := _debug_contexts.get(mt) as TreeProbe
		if is_instance_valid(probe):
			if watch:
				probe.watch_node(np)
			else:
				probe.unwatch_node(np)
		return


# Clones a local [MultiplayerTree] into its own window on an editor spawn
# request. The clone carries its whole configured subtree (including its
# in-window [ConnectBrowser]), self-registers offline through its own
# [TreeProbe] on entering the window, and is connected from that browser. The
# editor cannot add nodes to the game tree, so this game-side handler performs
# the [method Node.duplicate] the editor button asks for.
func _handle_spawn_tree(d: Dictionary) -> void:
	var tree_name: String = d.get("tree_name", "")
	var template := _templates.get(tree_name) as MultiplayerTree
	if template == null:
		_dbg.warn(
			"Reporter: [SpawnTree] no template for local tree '%s'." % [tree_name],
			func(m: String) -> void: push_warning(m),
		)
		return

	var clone := template.duplicate() as MultiplayerTree
	if clone == null:
		return
	clone.name = "%s_dbg%d" % [tree_name, _spawned_windows.size() + 1]
	# A spawned clone waits to be connected from its own in-window ConnectBrowser;
	# it must not fire the template's dev auto-connect on _ready.
	clone.debug_join = null
	Netw.dbg.mark_debug_tree(clone)

	# The template was snapshotted after the tree auto-created its HostSceneView
	# (multiplayer_tree._enter_tree), so the clone carries a stale one AND
	# recreates a fresh one on entry. Drop the stale copies; the clone rebuilds
	# exactly one through _ensure_host_scene_view.
	for stale: Node in clone.find_children("*", "HostSceneView", true, false):
		stale.free()

	var window := ParticipantWindow.new()
	window.title = clone.name
	window.size = Vector2i(960, 600)
	window.tree = clone
	window.close_requested.connect(_close_spawned_window.bind(window))
	# Child of the window so it renders into the window's viewport; adding the
	# window (below) enters the clone into the SceneTree, firing its offline
	# register_tree. Detached until then so no half-built parent is mid-setup.
	window.add_child(clone)
	_spawned_windows.append(window)
	add_child(window)
	window.owner = self
	clone.owner = window
	# Center in the host viewport so the (embedded) window's drag header lands
	# inside the originating window rather than off its top edge.
	var vp := get_viewport()
	if vp:
		var avail := Vector2i(vp.get_visible_rect().size)
		window.position = ((avail - window.size) / 2).max(Vector2i.ZERO)
	window.show()

	_dbg.info("Reporter: [SpawnTree] cloned '%s' as '%s'." % [tree_name, clone.name])


func _close_spawned_window(window: Window) -> void:
	if window not in _spawned_windows:
		return
	_spawned_windows.erase(window)

	var pw := window as ParticipantWindow
	var mt: MultiplayerTree = pw.tree if pw else null
	if is_instance_valid(mt):
		if mt.state == NetwSessionInterface.State.CONNECTING:
			mt.abort_join()
		elif mt.state != NetwSessionInterface.State.OFFLINE:
			await mt.leave()

	if is_instance_valid(window):
		window.queue_free()


# Toggles a debugger-spawned tree's window between embedded and native.
# [member Window.force_native] flips the window out of the game's embedded
# subwindow surface into its own OS window (and back), so a dev on a tiling
# WM can pop a participant out or fold it back into one window.
func _handle_toggle_embed(d: Dictionary) -> void:
	var tree_name: String = d.get("tree_name", "")
	for w: Window in _spawned_windows:
		var pw := w as ParticipantWindow
		if pw == null or not is_instance_valid(pw.tree):
			continue
		if pw.tree.get_tree_name() == tree_name:
			# force_native cannot change on a displayed window, so hide across
			# the flip and restore visibility.
			var was_visible := pw.visible
			pw.hide()
			pw.force_native = not pw.force_native
			if was_visible:
				pw.show()
			return


# Forwards a node inspection request from the editor.
func _handle_inspect_node(d: Variant) -> void:
	if not EngineDebugger.is_active() or not is_inside_tree():
		return

	var path: String = ""
	var pid: int = 0

	if d is String:
		path = d
	elif d is Dictionary:
		path = d.get("node_path", "")
		pid = d.get("peer_id", 0)

	var node: Node = get_tree().root.get_node_or_null(path)

	# If path lookup failed, try finding the player node by peer_id.
	if not is_instance_valid(node) and pid != 0:
		for mt in _trees:
			for player in mt.get_all_players():
				if player.owner.get_multiplayer_authority() == pid:
					node = player.owner
					break
			if node:
				break

	if is_instance_valid(node):
		var snapshot := [node.get_instance_id(), node.get_class(), []]
		EngineDebugger.send_message("remote_objects_selected", [snapshot])


func _handle_visualizer_toggle(d: Dictionary) -> void:
	# Visualizer toggles (like nameplates) are local debug session preferences.
	# We apply them to every MultiplayerTree in this process so they manifest
	# on whatever view the user is looking at.
	for context in _debug_contexts.values():
		context.apply_command(d)


func _on_window_geometry_reported(rect: Rect2i) -> void:
	if not EngineDebugger.is_active():
		return
	if rect.size.x <= 0 or rect.size.y <= 0:
		return
	var mt := _first_valid_tree()
	var payload := {
		"position": rect.position,
		"size": rect.size,
	}
	if is_instance_valid(mt):
		emit_debug_event("networked:window_geometry", payload, mt)
	else:
		EngineDebugger.send_message("networked:window_geometry", [payload])

# --- Message Queue ------------------------------------------------------------


func _queue(
		msg_name: String,
		data: Dictionary,
		mt: MultiplayerTree = null,
) -> void:
	_message_queue.append([msg_name, data, mt])
	if not _flush_pending:
		_flush_pending = true
		call_deferred("_flush_queue")


func _flush_queue() -> void:
	_flush_pending = false
	_flush_now()


func _flush_now() -> void:
	if not _debug_build():
		_message_queue.clear()
		_cycle_peer_events.clear()
		return

	var mt: MultiplayerTree = null
	if not _trees.is_empty():
		mt = _trees[0]

	if _telemetry:
		var active_span := Netw.dbg.active_span()
		var cid_trail: Array = [str(active_span.id)] if active_span else ["N/A"]
		_telemetry.record(
			Engine.get_process_frames(),
			cid_trail,
			[],
			_cycle_peer_events,
			{ },
		)
	_cycle_peer_events.clear()

	for entry: Array in _message_queue:
		var entry_mt = entry[2] if entry.size() >= 3 else null

		if not is_instance_valid(entry_mt):
			if not mt:
				_dbg.warn(
					"Reporter: [QueueDrop] '%s' - no tree context",
					[entry[0]],
				)
				continue
			entry_mt = mt

		emit_debug_event(entry[0], entry[1], entry_mt)
	_message_queue.clear()


func _first_valid_tree() -> MultiplayerTree:
	for mt: MultiplayerTree in _trees:
		if is_instance_valid(mt):
			return mt
	return null

# --- Telemetry Helpers --------------------------------------------------------


# Freezes the ring buffer and returns its snapshot.
func _freeze_and_slice() -> Array:
	if _telemetry:
		_telemetry.freeze()
		return _telemetry.snapshot(20)
	return []


# Emits a [NetwFinding] through the single detection pipeline.
# [br][br]
# Owns rate-limiting (by [member NetwFinding.trigger]), base-field filling,
# span failure, transport, and the [code]on_violation[/code] fan-out, so
# detectors never re-implement that glue. The [member _is_sending_manifest]
# guard also brackets the fan-out, so a listener that triggers another finding
# is dropped rather than recursing.
func _emit_finding(finding: NetwFinding, mt: MultiplayerTree) -> void:
	if _is_sending_manifest:
		return
	_is_sending_manifest = true

	if _rate_limited(finding.trigger):
		_dbg.error(
			"Reporter: [RateLimit] Manifest blocked for trigger: %s",
			[finding.trigger],
		)
		_maybe_break()
		_is_sending_manifest = false
		return

	_fill_base(finding.manifest, finding.trigger, finding.cid, mt)

	if finding.span:
		finding.span.fail(finding.reason, finding.data)

	_ship_manifest(finding.manifest, mt)

	for cb: Callable in _violation_listeners:
		if cb.is_valid():
			cb.call(finding)

	_maybe_break()
	_is_sending_manifest = false


## Registers [param cb] to receive every [NetwFinding] the pipeline emits.
func add_violation_listener(cb: Callable) -> void:
	if cb.is_valid() and cb not in _violation_listeners:
		_violation_listeners.append(cb)


# Advances the per-trigger rate-limit counters and returns [code]true[/code]
# when [param trigger] is over its per-second or per-minute cap.
func _rate_limited(trigger: String) -> bool:
	var now := Time.get_ticks_msec()

	if now - _last_manifest_sec_msec.get(trigger, 0) > 1000:
		_manifest_count_sec[trigger] = 0
		_last_manifest_sec_msec[trigger] = now
	if now - _last_manifest_min_msec.get(trigger, 0) > 60000:
		_manifest_count_min[trigger] = 0
		_last_manifest_min_msec[trigger] = now

	_manifest_count_sec[trigger] = _manifest_count_sec.get(trigger, 0) + 1
	_manifest_count_min[trigger] = _manifest_count_min.get(trigger, 0) + 1

	return _manifest_count_sec[trigger] > _MANIFEST_MAX_PER_SEC \
			or _manifest_count_min[trigger] > _MANIFEST_MAX_PER_MIN


# Validates and ships [param manifest] over the debugger transport, or pushes
# an error when no local editor session is listening. Assumes the caller holds
# the [member _is_sending_manifest] guard.
func _ship_manifest(manifest: NetwManifest, mt: MultiplayerTree) -> void:
	manifest.validate_contract()
	var payload := manifest.to_dict()
	_dbg.info(
		"Reporter: [SendManifest] %s (cid=%s)" % \
				[manifest.trigger, manifest.cid],
	)

	var target_mt := mt
	if not is_instance_valid(target_mt) and manifest._mt:
		target_mt = manifest._mt.get_ref() as MultiplayerTree

	if not is_instance_valid(target_mt):
		_dbg.warn(
			"Reporter: [ManifestDrop] '%s' - no valid tree",
			[manifest.trigger],
		)
		return

	if _has_local_session():
		emit_debug_event("networked:crash_manifest", payload, target_mt)
	else:
		push_error(
			"[NETWORKED] %s | scene=%s peer=%d errors=%s" % [
				manifest.trigger,
				manifest.active_scene,
				manifest.network_state.get("peer_id", 0),
				str(payload.get("errors", [])),
			],
		)


# Fills the common base fields of [param m] from engine state.
func _fill_base(
		m: NetwManifest,
		trigger: String,
		cid_val: String,
		mt: MultiplayerTree,
) -> void:
	m.trigger = trigger
	m.cid = cid_val
	m._mt = weakref(mt) if is_instance_valid(mt) else null
	m.cid_timeline = [cid_val]
	m.frame = Engine.get_process_frames()
	# Preserve a timestamp a detector set explicitly (e.g. the C++ error's own
	# capture time); otherwise stamp it now.
	if m.timestamp_usec == 0:
		m.timestamp_usec = Time.get_ticks_usec()

	var ctx := _debug_contexts.get(mt) as TreeProbe
	m.active_scene = ctx.get_active_scene_path() if ctx else "?"

	# Merge base keys into any manifest-specific extras a detector pre-set, so a
	# finding can populate network_state before the pipeline fills the base.
	m.network_state["is_server"] = (
			mt.is_host if (is_instance_valid(mt) and mt.role != NetwSessionInterface.Role.NONE) else false
	)
	m.network_state["role"] = \
	mt.role if is_instance_valid(mt) else NetwSessionInterface.Role.NONE
	m.network_state["role_name"] = _role_name(mt)
	m.network_state["tree_name"] = mt.get_tree_name() if is_instance_valid(mt) else ""
	m.network_state["peer_id"] = mt.multiplayer_api.get_unique_id() if \
	is_instance_valid(mt) and mt.multiplayer_api else 0
	m.telemetry_slice = _freeze_and_slice()


func _role_name(mt: MultiplayerTree) -> String:
	if not is_instance_valid(mt):
		return NetwSessionInterface.Role.keys()[NetwSessionInterface.Role.NONE]
	return NetwSessionInterface.Role.keys()[mt.role]


# Pauses the engine if break is enabled.
func _maybe_break() -> void:
	if EngineDebugger.is_active() and _auto_break:
		breakpoint

# --- Unified send -------------------------------------------------------------


## Single dispatch point for all outgoing debug messages.
func emit_debug_event(
		msg: String,
		data: Dictionary,
		mt: MultiplayerTree,
) -> void:
	if not _debug_build() or not is_instance_valid(mt):
		return

	var envelope := NetwEnvelope.from_mt(mt, msg, data, reporter_id)
	var bytes := var_to_bytes(envelope.to_dict())

	if _has_local_session():
		_trace_emit("[EmitDirect]", msg, "(path=%s)" % [envelope.source_path])
		EngineDebugger.send_message("networked:envelope", [bytes])
	else:
		_dbg.warn(
			"Reporter: [EmitDropped] %s - no active debugger session",
			[msg],
		)


func _trace_emit(prefix: String, msg: String, extra: String = "") -> void:
	if msg == "networked:clock_sample" or msg.begins_with("networked:span_"):
		return

	if extra:
		_dbg.trace("Reporter: %s %s %s" % [prefix, msg, extra])
	else:
		_dbg.trace("Reporter: %s %s" % [prefix, msg])

# --- Guards -------------------------------------------------------------------


# True in editor runs and exported debug builds.
static func _debug_build() -> bool:
	if not _reporting_checked:
		_reporting_checked = true
		_reporting_enabled = true

		if Netw.is_test_env():
			_reporting_enabled = false
		else:
			var args := OS.get_cmdline_args()
			for arg in args:
				if arg == "--headless":
					_reporting_enabled = false
					break

	return _reporting_enabled and OS.has_feature("debug")


# True when this process has a live [EngineDebugger] connection.
static func _has_local_session() -> bool:
	return EngineDebugger.is_active()
