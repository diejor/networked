@tool
## Entry point for the Networked addon.
##
## Use [method of] to obtain the [NetwMultiplayer] session for any node inside a
## multiplayer session. It is the single front door to the session surface:
## [br]- session verbs and state: [method NetwMultiplayer.host],
##   [method NetwMultiplayer.pause], [method NetwMultiplayer.kick],
##   [member NetwMultiplayer.role], [member NetwMultiplayer.participants].
## [br]- backend systems such as [member NetwMultiplayer.clock] and
##   [member NetwMultiplayer.scene_manager], plus custom services you register
##   yourself through [method NetwMultiplayer.get_service].
## [br]- [member NetwMultiplayer.connect]: pre-game connect / server browser
##   API (host, join, target list). See [NetwConnect].
## [br]- [member NetwMultiplayer.interest] and [member NetwMultiplayer.liveness].
## [br]For positional questions, [method NetwEntity.of] resolves the entity for a
## node and [method NetwScene.for_node] resolves its scene.
##
## [br][br]
## [b]Other Entry Points[/b]
##
## [br]- [member dbg]: structured logging and causal tracing. See
##   [NetwDbg].
##
## [br][br][b]Replicated Properties[/b]
## [br]Declare a variable's delivery once with [method configure_property] and
## write the variable normally afterwards. A field marked
## [method NetwScriptModel.PropertyConfig.state],
## [method NetwScriptModel.PropertyConfig.input], or
## [method NetwScriptModel.PropertyConfig.broadcast] joins one of its script's
## per-tick sets, and the sync pump on [NetwSyncPipeline] ships every change
## automatically. An unmarked field syncs only when [method sync_property]
## pushes it, the on-demand door for values that change on events rather than
## every tick.
## [codeblock]
## func _init() -> void:
##     Netw.configure_property(self, &"position").state()   # pumped per tick
##     Netw.configure_property(self, &"fuel")                # on demand
##
## func refuel() -> void:
##     fuel = 100.0
##     Netw.sync_property(self, &"fuel")                     # explicit push
## [/codeblock]
## [enum NetwSyncSet.Record] compares the three per-tick kinds, and
## [NetwScriptModel.PropertyConfig] holds the full per-field and per-set
## delivery surface.
##
## [br][br][b]Entity RPC[/b]
## [br]The [code]Netw.rpc[/code] family calls annotated methods on a networked
## entity, addressed by the entity's route instead of a node path, so a call never
## errors on a node that has not spawned yet (see [NetwReplicationInterface]). Declare the
## method with Godot's [code]@rpc[/code], which supplies the authority mode and
## reliable or unreliable transport, and register it once with
## [method configure_rpc]. Arguments are a flat list, and a [NetwEntity] or entity
## root [Node] passed as an argument crosses the wire as its route and arrives live.
## [codeblock]
## # receiver, once (e.g. in _init):
## Netw.configure_rpc(self.apply_stun)
##
## @rpc("authority", "reliable")
## func apply_stun(attacker: Node) -> void:
##     play_stun(attacker)
##
## # server, fan out to every peer that has this entity:
## Netw.rpc(target.apply_stun, attacker_root)
## [/codeblock]
## Pick the verb: [method rpc] and [method rpc_id] send RPC events, where
## [method rpc_id] targets a single peer (or [code]0[/code] for broadcast).
## [method request] and [method request_id] make two-way calls, where
## [method request] targets the Server, and [method request_id] a single peer.
## [method request_all] broadcasts a request to all clients, returning a
## [NetwGroupPromise]. [method rpc_controller] and [method request_controller]
## target the entity's controller. [method sync_property] and
## [method emit_entity_signal] replicate variables and signals on demand,
## gated by [method configure_property] and [method configure_signal] write
## policies. [method channel] opens a raw [NetwChannel] for traffic that
## has no entity.
##
## [br][br]
## A fan-out is not a true broadcast. [method rpc] and [method request_all] reach
## only the peers that currently see the entity through [NetwInterestInterface], so a
## peer the interest layer does not admit never receives the call.
##
## [br][br][b]Configure in _init[/b]
## [br]Call every [code]configure_*[/code] from [method Object._init], before the
## entity's route goes live and any frame can target it.
## [codeblock]
## _init()        configure_* registers policy and codec    <- in time
## route live     the first frame can arrive
## _ready()       a configure_* here races that frame: a quantized
##                argument decodes wrong, a write is rejected, and a
##                reliable event gets no second chance
## [/codeblock]
##
## [br][br]
## [code]any_peer[/code] RPCs broadcast to every peer when called with
## [code].rpc()[/code] (always use [code].rpc_id(1)[/code] for client-to-server
## requests and validate the sender inside the handler).
##
## [br][br]
## [method of] returns [code]null[/code] outside a [MultiplayerTree] branch. A
## cached [NetwMultiplayer] stays a valid object across disconnects and reports
## inactivity through [method NetwMultiplayer.is_active] instead of dangling.
class_name Netw
extends Object

## Static entry point for all debug and logging functionality.
static var dbg: NetwDbg = NetwDbg.new()


## Resolves the [NetwMultiplayer] session enclosing [param node].
##
## Returns [code]null[/code] outside a [MultiplayerTree] branch or off-tree.
## This is the single user-facing entry point for session state, the owned
## interfaces ([member NetwMultiplayer.clock], [member NetwMultiplayer.liveness],
## [member NetwMultiplayer.interest], ...), and the session verbs.
static func of(node: Node) -> NetwMultiplayer:
	return NetwMultiplayer.of(node)


static var _is_test_env_cached: Variant = null


## Returns [code]true[/code] if the current process is running under a GdUnit4
## test environment.
static func is_test_env() -> bool:
	if _is_test_env_cached != null:
		return _is_test_env_cached
	var res := false
	if Engine.has_meta("GdUnitRunner"):
		res = true
	else:
		var args := OS.get_cmdline_args() + OS.get_cmdline_user_args()
		for arg in args:
			if "GdUnit" in arg or "GdUnitTestRunner.tscn" in arg:
				res = true
				break
	_is_test_env_cached = res
	return res

# ---------------------------------------------------------------------------
# Configuration builders and registry
# ---------------------------------------------------------------------------


## The project-wide provider a probe reply is built from, and the script that
## registered it. Held statically because a game registers it from
## [method Object._init] before any session exists. A [NetwServerInfo] carries
## no live state, so one provider serves every session that lacks a per-session
## override.
static var _server_info_provider: Callable
static var _server_info_provider_script: Script


## Registers the project-wide provider that builds the [NetwServerInfo] a probe
## answers with.
##
## [param provider] takes the resolved [NetwMultiplayer] and returns a
## [NetwServerInfo]. Register it from [method Object._init] on a script present
## in both builds. A session with no provider and no per-session override answers
## with [method NetwServerInfo.from_session]. Registering from a second script
## asserts, matching every single-slot registrar.
## [codeblock]
## func _init() -> void:
##     Netw.configure_server_info(func(api: NetwMultiplayer) -> NetwServerInfo:
##         var info := NetwServerInfo.new()
##         info.motd = "Friday night session"
##         info.players = api.participants.size()
##         info.max_players = 8
##         return info)
## [/codeblock]
static func configure_server_info(provider: Callable) -> void:
	var object := provider.get_object()
	var script: Script = object.get_script() if object else null
	assert(
		_server_info_provider_script == null
			or _server_info_provider_script == script,
		"configure_server_info: already registered by another script.",
	)
	_server_info_provider = provider
	_server_info_provider_script = script


## Returns the project-wide server-info provider, or an invalid [Callable] when
## none was registered. [NetwProbeResponder] consults it after a per-session
## override and before [method NetwServerInfo.from_session].
static func resolve_server_info_provider() -> Callable:
	return _server_info_provider


## The project-wide join handler, the [NetwScriptModel.ConnectConfig] carrying its
## wire schema, and the script that registered it. Held statically because a game
## registers the handler from [method Object._init] before any session exists.
static var _join_handler: Callable
static var _join_config: NetwScriptModel.ConnectConfig
static var _join_handler_script: Script


## Registers the project-wide server handler invoked once for each accepted join
## and returns its [NetwScriptModel.ConnectConfig] for optional
## [method NetwScriptModel.ConnectConfig.quantize] of the wire args.
##
## The handler's first parameter is the framework-owned [ResolvedJoin]. Its
## remaining parameters are the wire schema a joining client fills with typed
## args ([member JoinPayload.arg_values]). Register it from [method Object._init]
## on a script present in both builds. With no registration and no per-session
## override, a session resolves the built-in [NetwDefaultJoin]. Registering from
## a second script asserts.
## [codeblock]
## func _init() -> void:
##     Netw.configure_join(spawn_at)
##
## func spawn_at(rj: ResolvedJoin, point: StringName, team: int) -> MultiplayerScene:
##     ...
## [/codeblock]
## [br][br][b]Server Only.[/b]
static func configure_join(handler: Callable) -> NetwScriptModel.ConnectConfig:
	var object := handler.get_object()
	var script: Script = object.get_script() if object else null
	assert(
		_join_handler_script == null or _join_handler_script == script,
		"configure_join: already registered by another script.",
	)
	_join_handler = handler
	_join_handler_script = script
	var cfg := NetwScriptModel.ConnectConfig.new()
	cfg.context_script = script
	cfg.context_name = handler.get_method()
	_join_config = cfg
	return cfg


## Returns the project-wide join handler, or an invalid [Callable] when none was
## registered. [NetwSessionInterface] consults it after a per-session override
## and before the built-in [NetwDefaultJoin].
static func resolve_join_handler() -> Callable:
	return _join_handler


## Returns the wire-arg quantizers declared for the project-wide join handler.
static func resolve_join_quantizers() -> Array:
	return _join_config.quantizers if _join_config else []


## The project-wide auth-flow factory and the script that registered it. Held
## statically because a game registers it from [method Object._init] before any
## session exists. Each session calls the factory to construct its own
## [NetwAuthFlow], so per-attempt state never leaks between sessions.
static var _auth_factory: Callable
static var _auth_factory_script: Script


## Registers the project-wide factory that builds a [NetwAuthFlow] per session.
##
## [param factory] takes the resolved [NetwMultiplayer] and returns a fresh
## [NetwAuthFlow]. Register it from [method Object._init] on a script present in
## both builds. With no factory and no per-session override, a session admits
## peers without verifying credentials, trusting the client-claimed username. A
## user-set [member NetwMultiplayer.auth_callback] still takes precedence over
## the flow. Registering from a second script asserts.
## [codeblock]
## func _init() -> void:
##     Netw.configure_auth(func(api: NetwMultiplayer) -> NetwAuthFlow:
##         return SteamAuthFlow.new(api))
## [/codeblock]
static func configure_auth(factory: Callable) -> void:
	var object := factory.get_object()
	var script: Script = object.get_script() if object else null
	assert(
		_auth_factory_script == null or _auth_factory_script == script,
		"configure_auth: already registered by another script.",
	)
	_auth_factory = factory
	_auth_factory_script = script


## Returns the project-wide auth-flow factory, or an invalid [Callable] when none
## was registered. [NetwSessionInterface] consults it after a per-session
## override to construct the session's flow.
static func resolve_auth_factory() -> Callable:
	return _auth_factory


## Installs the server policy used by [method NetwSceneInterface.request_change].
##
## [param handler] must be bound beneath an active Networked session. It
## receives [code](participant, scene_name, args)[/code] and returns
## [code]true[/code] to allow the request.
## [br][br][b]Server Only.[/b]
static func configure_scene_change(handler: Callable) -> void:
	var object := handler.get_object()
	assert(object is Node, "Scene change policy must be bound to a Node.")
	var api := NetwMultiplayer.of(object as Node)
	assert(api != null, "Scene change policy requires an active session.")
	api.scenes.set_change_request_handler(handler)


## Marks [param scene_type]'s root script as a multiplayer scene, so
## [method is_multiplayer_scene] reports it.
##
## This is a lightweight declaration, not an authorization. It never decides
## whether a client may reach the scene, which is always the
## [method configure_scene_change] policy's call. Register from the scene root's
## [method Object._static_init] so loading the script marks it, letting a
## dedicated server introspect it without instantiating. At minimum, mark every
## scene the session replicates; add [method configure_multiplayer_scene] to opt
## the scene into the native-change on-ramp.
## [codeblock]
## static func _static_init() -> void:
##     Netw.mark_multiplayer_scene(Arena)
## [/codeblock]
static func mark_multiplayer_scene(scene_type: Variant) -> void:
	var script := _resolve_scene_script(scene_type)
	assert(
		script != null,
		"mark_multiplayer_scene: expected a scene root script or class.",
	)
	NetwScriptModel._multiplayer_scene_marks[script] = true


## Returns whether [param script] is a registered multiplayer scene root.
##
## Convenience introspection, backed by [method mark_multiplayer_scene] and
## [method configure_multiplayer_scene]. It is never an authorization gate.
static func is_multiplayer_scene(script: Script) -> bool:
	return NetwScriptModel.is_scene_marked(script)


## Opts scene instance [param node] into the native-change on-ramp and returns
## its fluent [NetwScriptModel.SceneMarkConfig].
##
## Call from the scene root's [method Object._init] so every instantiation,
## framework spawn or native [method Node.change_scene_to_file], carries the
## detach hook. On a native change during a live session the hook detaches the
## local instance and issues a [method NetwSceneInterface.request_change_path]; a
## framework spawn is recognized through
## [member NetwReplicationInterface.is_applying_remote_frame] and left alone.
## The server still decides the request through [method configure_scene_change].
## [codeblock]
## func _init() -> void:
##     Netw.configure_multiplayer_scene(self).timeout(8.0)
## [/codeblock]
static func configure_multiplayer_scene(
		node: Node,
) -> NetwScriptModel.SceneMarkConfig:
	assert(
		node != null,
		"configure_multiplayer_scene: a scene root node is required.",
	)
	var script := node.get_script() as Script
	var config := NetwScriptModel.get_scene_config(script)
	if config == null:
		config = NetwScriptModel.SceneMarkConfig.new()
		config.context_script = script
		NetwScriptModel._multiplayer_scene_configs[script] = config
	node.tree_entered.connect(
		_on_marked_scene_entered.bind(node),
		CONNECT_ONE_SHOT,
	)
	return config


# Runs the detach decision table when a marked instance enters the tree.
static func _on_marked_scene_entered(node: Node) -> void:
	if not is_instance_valid(node):
		return
	var sessions := NetwMultiplayer.live_sessions()
	if sessions.size() != 1:
		# No session, or an ambiguous multi-session host (the test harness). A
		# native run stays local until exactly one session owns the presentation.
		return
	var api := sessions[0]
	if api.replication.is_applying_remote_frame:
		return
	if api.state != NetwSessionInterface.State.ONLINE:
		return
	api.scenes._handle_native_scene_entry(node)


# Resolves a scene root script from a class, a script, or a scripted instance.
static func _resolve_scene_script(scene_type: Variant) -> Script:
	if scene_type is Script:
		return scene_type
	if scene_type is Object:
		return (scene_type as Object).get_script() as Script
	return null


## Registers RPC config for the callable script method and returns the fluent
## [NetwScriptModel.EventConfig] builder.
##
## Configuration is keyed per script, so every instance re-declares onto the
## same config from [method Object._init]. A sub-node target also registers
## itself as an entity component through
## [method NetwEntity.register_component], so an entity RPC addresses it by a
## 1-byte id instead of a path string.
static func configure_rpc(callable: Callable) -> NetwScriptModel.EventConfig:
	var obj := callable.get_object()
	var method := callable.get_method()
	var script: Script = obj.get_script() if obj else null
	assert(script != null, "configure_rpc: Callable must be bound to an object with a script.")

	# Per-instance: register the target as an entity component. Runs even when
	# the config below is already cached for this script.
	if obj is Node:
		var node := obj as Node
		if node.is_inside_tree() and not is_test_env():
			Netw.dbg.warn(
				"configure_rpc: Method '%s' configured on '%s' after entering the tree. "
				+ "Configure in _init() to prevent early packet race conditions.",
				[method, node.name],
				func(m): push_warning(m)
			)
		_inject_component_registration(node)

	var methods_map: Dictionary = (
			NetwScriptModel._rpc_configs.get_or_add(script, { })
	)
	if methods_map.has(method):
		return methods_map[method]

	var opt := NetwScriptModel.EventConfig.new()
	opt.context_script = script
	opt.context_name = method
	opt.context_type = 0
	if script.get_rpc_config().has(method):
		var cfg: Dictionary = script.get_rpc_config()[method]
		opt.is_call_local = cfg.get("call_local", false)
	methods_map[method] = opt
	return opt


# Registers a configured node as a component of its entity so entity RPC
# routing can compress its relative path to a 1-byte id. A node configured
# while orphaned registers when it enters the tree. The entity root is skipped
# by NetwEntity.register_component itself.
static func _inject_component_registration(node: Node) -> void:
	if node.has_meta(&"_netw_comp_injected"):
		return
	node.set_meta(&"_netw_comp_injected", true)
	if node.is_inside_tree():
		_register_as_component(node)
		return
	node.tree_entered.connect(
		_register_as_component.bind(node),
		CONNECT_ONE_SHOT,
	)


static func _register_as_component(node: Node) -> void:
	if not is_instance_valid(node):
		return
	var entity := NetwEntity.of(node)
	if entity:
		entity.register_component(node)
	else:
		Netw.dbg.warn(
			"Node '%s' has networked configurations but is not part of a NetwEntity.",
			[node.name],
			func(m): push_warning(m)
		)
	_setup_defer_latches(node)


# Latches whether each configured defer signal has fired on this instance. The
# call router cannot ask "has this arbitrary signal fired yet" the way it can
# ask is_node_ready(), so a one-shot connection records it. The `ready` signal
# is handled directly through is_node_ready() and needs no latch.
static func _setup_defer_latches(node: Node) -> void:
	var script := node.get_script() as Script
	if not script:
		return
	var methods_map: Dictionary = NetwScriptModel._rpc_configs.get(script, { })
	for method in methods_map:
		var opt: NetwScriptModel.SyncConfig = methods_map[method]
		var sig := opt.defer_signal_name
		if sig.is_empty() or sig == &"ready":
			continue
		if not node.has_signal(sig):
			continue
		var meta_key := "_netw_fired_" + String(sig)
		if node.has_meta(meta_key):
			continue
		node.set_meta(meta_key, false)
		node.connect(
			sig,
			_mark_signal_fired.bind(node, meta_key),
			CONNECT_ONE_SHOT,
		)


static func _mark_signal_fired(node: Node, meta_key: String) -> void:
	if is_instance_valid(node):
		node.set_meta(meta_key, true)


# Returns whether signal_name's defer gate has been satisfied on node. ready
# maps to Node.is_node_ready(). Any other signal maps to whether it has fired
# since the node registered with configure_rpc(). A deferred entity call waits
# for this before running.
static func _defer_signal_fired(node: Node, signal_name: StringName) -> bool:
	if signal_name == &"ready":
		return node.is_node_ready()
	return bool(node.get_meta("_netw_fired_" + String(signal_name), false))


## Registers replication config for [param property] on [param node] and
## returns the fluent [NetwScriptModel.PropertyConfig] builder.
##
## Configuration is keyed per script and declared once from
## [method Object._init]. A bare configuration opens the on-demand
## [method sync_property] door with its write policy and codec, and a kind mark
## ([method NetwScriptModel.PropertyConfig.state],
## [method NetwScriptModel.PropertyConfig.input],
## [method NetwScriptModel.PropertyConfig.broadcast]) enrolls the field in a
## per-tick set the sync pump on [NetwSyncPipeline] ships automatically, with
## no send call in the gameplay code.
## [codeblock]
## func _init() -> void:
##     Netw.configure_property(self, &"position").state()       # pumped per tick
##     Netw.configure_property(self, &"fuel")                    # on demand, server writes
##     Netw.configure_property(self, &"steering").controller()   # on demand, controller writes
## [/codeblock]
##
## Set [param warn_late] to [code]false[/code] for a display-only receive-path
## spec configured after tree entry, where no spawn packet race exists.
static func configure_property(
		node: Node,
		property: StringName,
		warn_late: bool = true,
) -> NetwScriptModel.PropertyConfig:
	var script := node.get_script() as Script
	assert(
		property in node,
		"configure_property: Property '%s' does not exist on '%s'."
		% [property, node.name],
	)
	if warn_late and node.is_inside_tree() and not is_test_env():
		Netw.dbg.warn(
			"configure_property: Property '%s' configured on '%s' after entering the tree. "
			+ "Configure in _init() to prevent early packet race conditions.",
			[property, node.name],
			func(m): push_warning(m)
		)
	if not script:
		return NetwScriptModel.configure_node_property(node, property)
	_inject_property_lint(node)
	_schedule_derived_registration(node)
	var props_map: Dictionary = (
			NetwScriptModel._property_configs.get_or_add(script, { })
	)
	if props_map.has(property):
		return props_map[property]
	var opt := NetwScriptModel.PropertyConfig.new()
	opt.is_property = true
	opt.context_script = script
	opt.context_name = property
	opt.context_type = 1
	opt.context_node_ref = weakref(node)
	props_map[property] = opt
	return opt


# Registers a property-configured node's derived state and input sets with its
# session once it is in the tree, where NetwMultiplayer.of resolves. A node
# self-registers through its own tree_entered rather than a tree-wide scan, so a
# marked node declared in _init joins the sync pump the moment it enters a live
# session, whether it is spawned, adopted, or attached at runtime.
static func _schedule_derived_registration(node: Node) -> void:
	if node.has_meta(&"_netw_derived_reg_scheduled"):
		return
	node.set_meta(&"_netw_derived_reg_scheduled", true)
	node.tree_exiting.connect(_unregister_derived_now.bind(node))
	if node.is_inside_tree():
		_register_derived_now(node)
	else:
		node.tree_entered.connect(_register_derived_now.bind(node))


# Registers a node's derived sets with the session it entered. Idempotent per
# node, so a re-entry re-resolves the session without duplicating the binding.
static func _register_derived_now(node: Node) -> void:
	if not is_instance_valid(node):
		return
	var api := NetwMultiplayer.of(node)
	if api:
		api.replication._sync_pipeline.register_derived(node)


# Drops a node's derived sets when it leaves its session, releasing rewind history
# a state set held. Resolved while the node is still in the tree, on tree_exiting.
static func _unregister_derived_now(node: Node) -> void:
	if not is_instance_valid(node):
		return
	var api := NetwMultiplayer.of(node)
	if api:
		api.replication._sync_pipeline.unregister_derived(node)


# Schedules the double-authority lint for a property-configured node once it is in
# the tree, where its entity's synchronizers are resolvable.
static func _inject_property_lint(node: Node) -> void:
	if node.has_meta(&"_netw_property_lint_injected"):
		return
	node.set_meta(&"_netw_property_lint_injected", true)
	if node.is_inside_tree():
		_lint_property_overlaps(node)
	else:
		node.tree_entered.connect(_lint_property_overlaps.bind(node), CONNECT_ONE_SHOT)


# Warns when a configured property is already driven by a synchronizer on the same
# entity, the same double-authority mistake the persistence lint flags. Best effort:
# a synchronizer that has not registered yet is simply not seen, never a false
# positive.
static func _lint_property_overlaps(node: Node) -> void:
	if not is_instance_valid(node):
		return
	var script := node.get_script() as Script
	if not script:
		return
	var configs: Dictionary = NetwScriptModel._property_configs.get(script, { })
	if configs.is_empty():
		return
	var entity := NetwEntity.of(node)
	if not entity or not is_instance_valid(entity.owner):
		return
	for property in configs:
		var config: NetwScriptModel.SyncConfig = configs[property]
		if config and config.is_interpolation_only():
			continue
		# A state or input mark IS the stream that governs the property, so
		# matching the set derived from its own declaration is not a conflict.
		# A persist-only config declares a save column and no wire write door,
		# so a synchronizer driving the same property is the intended pairing
		# (the persistence lint owns the genuinely unsafe overlap there).
		if config is NetwScriptModel.PropertyConfig:
			if config.in_state_set or config.in_input_set:
				continue
			if config.is_persisted and not config._policy_configured \
					and not config._transfer_configured \
					and config.quantizers.is_empty():
				continue
		var real_path := entity.property_path(node, property)
		if not real_path.is_empty() and entity.governs_property(real_path):
			Netw.dbg.warn(
				"Netw.configure_property: '%s' on '%s' is already governed by a "
				+ "synchronizer; a variable and a synchronizer must not both "
				+ "write it (double authority)",
				[property, node.name],
			)


## Allowlists [param sig] for [method emit_entity_signal] and returns its
## [NetwScriptModel.SyncConfig] to set the emit policy.
##
## A signal must be registered before a peer may drive its emission across the
## wire, and the returned builder declares who may, mirroring [method configure_property].
##
## [br][br][b]Note:[/b] Unlike [method Netw.configure_rpc] and
## [method Netw.configure_property] which default to
## [method NetwScriptModel.SyncConfig.call_remote], signal configurations
## default to [method NetwScriptModel.SyncConfig.call_local] (meaning the
## signal is also emitted on the local sender).
## [codeblock]
## func _init() -> void:
##     Netw.configure_signal(self.exploded)   # only the authority may broadcast it
## [/codeblock]
static func configure_signal(sig: Signal) -> NetwScriptModel.EventConfig:
	var node := sig.get_object() as Node
	assert(node != null, "configure_signal: Signal must be bound to a Node.")
	var signal_name := sig.get_name()
	var script := node.get_script() as Script
	assert(script != null, "configure_signal: Node must have a script.")
	if node.is_inside_tree() and not is_test_env():
		Netw.dbg.warn(
			"configure_signal: Signal '%s' configured on '%s' after entering the tree. "
			+ "Configure in _init() to prevent early packet race conditions.",
			[signal_name, node.name],
			func(m): push_warning(m)
		)
	var sigs_map: Dictionary = (
			NetwScriptModel._signal_configs.get_or_add(script, { })
	)
	if sigs_map.has(signal_name):
		return sigs_map[signal_name]
	var opt := NetwScriptModel.EventConfig.new()
	opt.is_call_local = true
	opt.context_script = script
	opt.context_name = signal_name
	opt.context_type = 2
	sigs_map[signal_name] = opt
	return opt

# ---------------------------------------------------------------------------
# Rpc, requests, var sync, and signals verbs
# ---------------------------------------------------------------------------


## Calls [param callable]'s [code]@rpc[/code] method on every peer that currently
## sees the entity through [NetwInterestInterface], with the given flat arguments.
##
## Reliable or unreliable follows the method's [code]@rpc[/code] transfer mode. A
## [NetwEntity] or entity root [Node] argument crosses as its route and arrives
## live. Register the method with [method configure_rpc] first.
static func rpc(callable: Callable, args: Array = []) -> void:
	rpc_id(0, callable, args)


## Calls [param callable]'s [code]@rpc[/code] method on the specified [param peer_id].
##
## If [param peer_id] is [code]0[/code], it fans out to all live peers.
static func rpc_id(peer_id: int, callable: Callable, args: Array = []) -> void:
	var rpc_interface := _resolve_rpc_interface(callable)
	if not rpc_interface:
		return
	rpc_interface.rpc_call(callable, args, peer_id)


## Calls [param callable]'s [code]@rpc[/code] method on the entity's
## [member NetwEntity.controller] only.
static func rpc_controller(callable: Callable, args: Array = []) -> void:
	var node := callable.get_object() as Node
	var entity := NetwEntity.of(node)
	var controller := entity.controller if entity else 0
	if controller > 0:
		rpc_id(controller, callable, args)


## Sends a two-way request to the specified [param peer_id] and returns a
## [NetwPromise] that settles with the handler's return value.
static func request_id(
		peer_id: int,
		callable: Callable,
		args: Array = [],
) -> NetwPromise:
	assert(
		peer_id != 0,
		"request_id: peer_id cannot be 0; use request_all for broadcasts",
	)
	var rpc_interface := _resolve_rpc_interface(callable)
	if not rpc_interface:
		return null
	return rpc_interface.request_call(peer_id, callable, args)


## Sends a two-way request to the server and returns a [NetwPromise] that settles
## with the handler's return value.
static func request(callable: Callable, args: Array = []) -> NetwPromise:
	return request_id(1, callable, args)


## Broadcasts a two-way request to every peer that currently sees the entity
## through [NetwInterestInterface] and returns a [NetwGroupPromise] that aggregates
## their replies.
##
## The awaited set is fixed at send time. See [NetwGroupPromise] for per-peer and
## all-done settling.
static func request_all(callable: Callable, args: Array = []) -> NetwGroupPromise:
	var rpc_interface := _resolve_rpc_interface(callable)
	if not rpc_interface:
		return null
	return rpc_interface.request_call_group(callable, args)


## Sends a two-way request to the entity's [member NetwEntity.controller] and
## returns a [NetwPromise] for its reply.
static func request_controller(callable: Callable, args: Array = []) -> NetwPromise:
	var node := callable.get_object() as Node
	var entity := NetwEntity.of(node)
	var controller := entity.controller if entity else 0
	if controller > 0:
		return request_id(controller, callable, args)
	return null


## Replicates the current value of [param property] on [param node] to its
## peers, the on-demand door for a field outside every per-tick set.
##
## The property must be allowlisted with [method configure_property], whose
## write policy decides whether this peer is allowed to push it. The server
## applies and rebroadcasts, a client requests the server.
## [codeblock]
## func refuel() -> void:
##     fuel = 100.0
##     Netw.sync_property(self, &"fuel")
## [/codeblock]
## A field marked [method NetwScriptModel.PropertyConfig.state],
## [method NetwScriptModel.PropertyConfig.input], or
## [method NetwScriptModel.PropertyConfig.broadcast] needs no call here, since
## the sync pump on [NetwSyncPipeline] already ships its changes every tick.
static func sync_property(node: Node, property: StringName) -> void:
	var api := _session_api(node)
	if api:
		api.replication.send_property(node, property)


## Emits [param sig] on the same node across [param sig]'s entity on every peer.
##
## The signal re-emits on the resolved node on each receiver, carrying the given
## flat arguments. The signal must be allowlisted with [method configure_signal],
## whose emit policy decides whether this peer may drive it.
## [codeblock]
## exploded.emit()                       # local listeners
## Netw.emit_entity_signal(exploded)     # and every peer's copy of this entity
## [/codeblock]
static func emit_entity_signal(sig: Signal, args: Array = []) -> void:
	var node := sig.get_object() as Node
	if not node:
		return
	var api := _session_api(node)
	if not api:
		return
	api.replication.send_signal(node, sig.get_name(), args)


# Server-or-offline test. An offline rig with no peer counts as server so unit
# suites can exercise the server-only verbs, mirroring the addon's other
# _is_server guards.
static func _is_local_server(node: Node) -> bool:
	if not is_instance_valid(node):
		return true
	var mp := node.multiplayer
	if not mp or mp.multiplayer_peer == null:
		return true
	return mp.is_server()


# Resolves the [NetwMultiplayer] session for a node through its enclosing
# [MultiplayerTree], so it also works while the branch API is not installed.
static func _session_api(node: Node) -> NetwMultiplayer:
	var mt := MultiplayerTree.resolve(node)
	return mt.api if mt else null


static func _resolve_rpc_interface(callable: Callable) -> NetwRpcInterface:
	var obj := callable.get_object()
	if not (obj is Node):
		Netw.dbg.error("Netw RPC target object must be a Node.")
		return null
	var node := obj as Node
	var entity := NetwEntity.of(node)
	if not entity:
		Netw.dbg.error("Netw RPC target node '%s' is not part of a NetwEntity.", [node.name])
		return null
	var api := _session_api(node)
	if not api:
		Netw.dbg.error("No session found for node '%s'.", [node.name])
		return null
	return api.rpc_interface

# ---------------------------------------------------------------------------
# Spawn verbs
# ---------------------------------------------------------------------------


## Registers [param callable] as a spawn function for [method spawn].
##
## A spawn function is registered like an RPC and invoked like one, except its
## returned orphan [Node] enters the spawn pipeline. It must live on a host
## object that exists on every peer and construct from its arguments only.
## Values captured through [method NetwScriptModel.SyncConfig.on_spawn] apply
## on receivers after the function runs, so arguments decide shape and spawn
## state carries state.
## [codeblock]
## func _init() -> void:
##     Netw.configure_spawn(_spawn_bullet)
##
## func _spawn_bullet(dir: Vector2, tier: int) -> Node:
##     var b := BulletScene.instantiate()
##     b.setup(dir, tier)
##     return b
## [/codeblock]
static func configure_spawn(callable: Callable) -> NetwScriptModel.SyncConfig:
	var obj := callable.get_object()
	var method := callable.get_method()
	var script: Script = obj.get_script() if obj else null
	assert(
		script != null,
		"configure_spawn: Callable must be bound to an object with a script.",
	)
	var methods_map: Dictionary = (
			NetwScriptModel._spawn_fn_configs.get_or_add(script, { })
	)
	if methods_map.has(method):
		return methods_map[method]
	var opt := NetwScriptModel.SyncConfig.new()
	opt.context_script = script
	opt.context_name = method
	opt.context_type = 0
	methods_map[method] = opt
	return opt


## Registers the despawn policy for [param node]'s script, applied by
## receiving peers when the entity's
## [constant NetwFrameEnvelope.Channel.DESPAWN] frame removes the node. See
## [NetwScriptModel.DespawnConfig].
static func configure_despawn(node: Node) -> NetwScriptModel.DespawnConfig:
	var script := node.get_script() as Script
	assert(
		script != null,
		"configure_despawn: Node must have a script.",
	)
	var existing: NetwScriptModel.DespawnConfig = (
			NetwScriptModel._despawn_configs.get(script)
	)
	if existing:
		return existing
	var cfg := NetwScriptModel.DespawnConfig.new()
	NetwScriptModel._despawn_configs[script] = cfg
	return cfg


## Registers the persistence policy for [param node]'s archetype, applied by the
## server through [NetwPersistenceInterface]. Names the database, table, snapshot
## cadence, and hydration timing shared by every field marked
## [method NetwScriptModel.PropertyConfig.persisted]. See
## [NetwScriptModel.PersistenceConfig].
## [codeblock]
## func _init() -> void:
##     Netw.configure_persistence(self) \
##             .database(preload("res://data/game.tres")) \
##             .table(&"players")
##     Netw.configure_property(self, &"gold").persisted()
## [/codeblock]
static func configure_persistence(node: Node) -> NetwScriptModel.PersistenceConfig:
	var script := node.get_script() as Script
	if not script:
		return NetwScriptModel.configure_node_persistence(node)
	var existing: NetwScriptModel.PersistenceConfig = (
			NetwScriptModel._persistence_configs.get(script)
	)
	if existing:
		return existing
	var cfg := NetwScriptModel.PersistenceConfig.new()
	NetwScriptModel._persistence_configs[script] = cfg
	return cfg


## Replicates the orphan [param node] to every peer, reconstructed from its
## [member Node.scene_file_path], and returns its stamped [NetwEntity].
##
## Identity ([member NetwEntity.route], [member NetwEntity.entity_id],
## [member NetwEntity.peer_id], [member NetwEntity.controller]) is valid when
## this returns and before the node's first [method Node._enter_tree], on
## every peer. The spawn is snapshotted at end-of-frame of tree entry, so the
## orphan window between this call and [method Node.add_child] is where async
## hydration belongs.
## [codeblock]
## var player := PlayerScene.instantiate()
## var entity := Netw.replicate(player, participant)
## await save.hydrate(entity)
## scene.add_child(player)
## [/codeblock]
## This static resolves the sole active session through
## [method NetwMultiplayer.live_sessions] and errors when more than one is
## active. Multi-session hosts call
## [method NetwReplicationInterface.replicate] on the session they mean.
## [br][br][b]Server Only.[/b]
static func replicate(node: Node, owner: NetwParticipant = null) -> NetwEntity:
	var api := _sole_session("replicate")
	return api.replication.replicate(node, owner) if api else null


## Constructs a node on every peer by running the spawn function [param fn]
## with [param args], returning the locally constructed node for the caller
## to place. Register [param fn] with [method configure_spawn] first.
## [codeblock]
## muzzle.add_child(Netw.spawn(_spawn_bullet, [dir, 2]))
## [/codeblock]
## The session resolves from the function's host node, so this form works
## with any number of live [MultiplayerTree]s.
## [br][br][b]Server Only.[/b]
static func spawn(fn: Callable, args: Array = [], owner: NetwParticipant = null) -> Node:
	var host := fn.get_object() as Node
	assert(
		host != null,
		"Netw.spawn: the spawn function must be a method on a Node host.",
	)
	var mt := MultiplayerTree.resolve(host)
	var api := mt.api if mt else null
	if not api:
		Netw.dbg.error(
			"Netw.spawn: no session found for spawn function host '%s'.",
			[host.name],
		)
		return null
	return api.replication.spawn(fn, args, owner)


# Resolves the one active NetwMultiplayer for the orphan-taking statics,
# which have no node to resolve a session from. Errors loudly when ambiguous.
static func _sole_session(verb: String) -> NetwMultiplayer:
	var sessions := NetwMultiplayer.live_sessions()
	if sessions.is_empty():
		Netw.dbg.error("Netw.%s: no active NetwMultiplayer session.", [verb])
		return null
	if sessions.size() > 1:
		Netw.dbg.error(
			"Netw.%s: %d sessions are active; call "
			+ "api.replication.%s on the session you mean (Netw.of(node)).",
			[verb, sessions.size(), verb],
		)
		return null
	return sessions[0]


## Opens the raw [NetwChannel] channel [param channel_id] for the tree enclosing
## [param node].
##
## [param channel_id] runs [code]100[/code] to [code]254[/code]. Use this for
## entity-less byte traffic such as voice or chat. See [NetwChannel].
static func channel(node: Node, channel_id: int) -> NetwChannel:
	var api := _session_api(node)
	assert(
		api != null,
		"Netw.channel: No session found for the given node.",
	)
	return NetwChannel.new(channel_id, api.replication)
