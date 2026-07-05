@tool
## Entry point for the Networked addon.
##
## Use [method ctx] to obtain a [NetwContext] from any node inside a
## multiplayer session. The context gives you access to three sub-facades:
## [br]- [member NetwContext.tree]: network operations (pause, kick,
##   disconnect). See [NetwTree].
## [br]- [member NetwContext.services]: backend systems such as the
##   [MultiplayerClock] and [MultiplayerSceneManager], plus custom services
##   you register yourself. See [NetwServices].
## [br]- [member NetwContext.connect]: pre-game connect / server browser
##   API (host, join, target list). See [NetwConnect].
## [br]- [member NetwContext.scene]: lobby logic (readiness gates,
##   countdowns, player waiting). See [NetwScene].
##
## [br][br]
## [b]Other Entry Points[/b]
##
## [br]- [member dbg]: structured logging and causal tracing. See
##   [NetwDbg].
##
## [br][br][b]Entity RPC[/b]
## [br]The [code]Netw.rpc[/code] family calls annotated methods on a networked
## entity, addressed by the entity's route instead of a node path, so a call never
## errors on a node that has not spawned yet (see [RelayService]). Declare the
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
## target the entity's controller. [method sync_var] and
## [method emit_entity_signal] replicate variables and signals on demand,
## gated by [method configure_var] and [method configure_signal] write
## policies. [method channel] opens a raw [NetwChannel] for traffic that
## has no entity.
##
## [br][br]
## A fan-out is not a true broadcast. [method rpc] and [method request_all] reach
## only the peers that currently see the entity through [NetwInterest], so a
## peer the interest layer does not admit never receives the call.
##
## [br][br][b]Configure in _init[/b]
## [br]Call every [code]configure_*[/code] from [method Object._init]. It runs at
## instantiation, before the entity's route goes live and therefore before any
## frame can target it, so the method's policy and argument codec are in place when
## the first call lands. A call that arrives before its config is registered misses
## them: a quantized argument decodes wrong, and a [method sync_var] or
## [method emit_entity_signal] write is rejected. A reliable event gets no second
## chance in that window, so the [code]_init[/code] rule is what keeps it correct.
##
## [br][br]
## [code]any_peer[/code] RPCs broadcast to every peer when called with
## [code].rpc()[/code] (always use [code].rpc_id(1)[/code] for client-to-server
## requests and validate the sender inside the handler).
##
## [br][br]
## Always check [method NetwContext.is_valid] before caching a context
## reference. The underlying [MultiplayerTree] may be freed during
## disconnect or scene changes.
class_name Netw
extends Object

## Defines the authority policy rules for variable synchronization and
## signal emissions.
enum Policy {
	## Only the node's multiplayer authority (the server) has permission.
	AUTHORITY = 0,
	## Only the entity's controller client has permission.
	CONTROLLER = 1,
	## Any peer has permission.
	ANY_PEER = 2,
}

## Static entry point for all debug and logging functionality.
static var dbg: NetwDbg = NetwDbg.new()


## Returns a [NetwContext] for [param node] by walking its ancestor chain.
##
## Shorthand for [method NetwContext.for_node].
static func ctx(node: Node) -> NetwContext:
	return NetwContext.for_node(node)


## Returns [code]true[/code] if the current process is running under a GdUnit4
## test environment.
static func is_test_env() -> bool:
	if Engine.has_meta("GdUnitRunner"):
		return true
	var args := OS.get_cmdline_args() + OS.get_cmdline_user_args()
	for arg in args:
		if "GdUnit" in arg or "GdUnitTestRunner.tscn" in arg:
			return true
	return false


# ---------------------------------------------------------------------------
# Facade v2 — configuration builders and registry
# ---------------------------------------------------------------------------



# Script configurations
static var _rpc_configs: Dictionary = {}
static var _var_configs: Dictionary = {}
static var _signal_configs: Dictionary = {}


## Chained options for a method registered with [method Netw.configure_rpc].
##
## Each setter returns the same object, so they read as one fluent line in
## [method Object._init]. Defaults are: no deferral, sender validated by the
## method's [code]@rpc[/code] mode, and arguments serialized without quantization.
## [codeblock]
## Netw.configure_rpc(self.aim) \
##     .quantize([NetwQuantizeAngle.new().bits(12), null]) \
##     .defer_until(self.ready)
## [/codeblock]
class RpcOptions:
	extends RefCounted

	## The StringName of the signal this RPC call is deferred until.
	var defer_signal_name: StringName = &""

	## If [code]true[/code], only the controller client or server may call
	## this RPC.
	var is_controller_only: bool = false

	## List of per-argument quantizers used to compress method payloads.
	var quantizers: Array = []

	## Holds a received call until [param sig] has fired on the target node,
	## instead of running it the moment the node is live. [code]self.ready[/code]
	## is the common choice.
	func defer_until(sig: Signal) -> RpcOptions:
		if not defer_signal_name.is_empty():
			Netw.dbg.warn(
				"RpcOptions: defer gate overridden multiple times",
				func(m): push_warning(m)
			)
		defer_signal_name = sig.get_name()
		return self

	## Restricts the caller to the entity's [member NetwEntity.controller] (or the
	## server), the policy for a client steering its own entity.
	func controller_only() -> RpcOptions:
		is_controller_only = true
		return self

	## Bit-packs arguments with a per-position [NetwQuantize] list, parallel to the
	## method's parameters. A [code]null[/code] entry leaves that argument
	## unquantized.
	func quantize(p_quantizers: Array) -> RpcOptions:
		if not quantizers.is_empty():
			Netw.dbg.warn(
				"RpcOptions: quantizers overridden multiple times",
				func(m): push_warning(m)
			)
		quantizers = p_quantizers
		return self


## Chained write policy for a variable registered with [method Netw.configure_var].
##
## The policy decides which peer [method Netw.sync_var] accepts a write from. The
## default is [method authority].
class VarOptions:
	extends RefCounted

	## The write policy rule.
	var write_policy: Policy = Policy.AUTHORITY:
		set(val):
			if _policy_configured:
				Netw.dbg.warn(
					"VarOptions: policy overridden multiple times",
					func(m): push_warning(m)
				)
			write_policy = val
			_policy_configured = true
	var _policy_configured := false

	## Only the node's multiplayer authority (the server) may write. The default.
	func authority() -> VarOptions:
		write_policy = Policy.AUTHORITY
		return self

	## Only the entity's [member NetwEntity.controller] may write. The server
	## validates and rebroadcasts.
	func controller() -> VarOptions:
		write_policy = Policy.CONTROLLER
		return self

	## Any peer may write. Use only for cosmetic state, the server does not
	## validate the value.
	func any_peer() -> VarOptions:
		write_policy = Policy.ANY_PEER
		return self


## Chained emit policy for a signal registered with [method Netw.configure_signal].
##
## The policy decides which peer may drive [method Netw.emit_entity_signal]. The
## default is [method authority]. Mirrors [Netw.VarOptions].
class SignalOptions:
	extends RefCounted

	## The emit policy rule.
	var emit_policy: Policy = Policy.AUTHORITY:
		set(val):
			if _policy_configured:
				Netw.dbg.warn(
					"SignalOptions: policy overridden multiple times",
					func(m): push_warning(m)
				)
			emit_policy = val
			_policy_configured = true
	var _policy_configured := false

	## If [code]true[/code], the signal is emitted locally on the sender
	## node as well.
	var is_call_local: bool = true:
		set(val):
			if _local_configured:
				Netw.dbg.warn(
					"SignalOptions: call_local/call_remote overridden multiple times",
					func(m): push_warning(m)
				)
			is_call_local = val
			_local_configured = true
	var _local_configured := false

	## Only the node's multiplayer authority (the server) may emit. The default.
	func authority() -> SignalOptions:
		emit_policy = Policy.AUTHORITY
		return self

	## Only the entity's [member NetwEntity.controller] may emit.
	func controller() -> SignalOptions:
		emit_policy = Policy.CONTROLLER
		return self

	## Any peer may emit. The receiver still checks the signal is registered.
	func any_peer() -> SignalOptions:
		emit_policy = Policy.ANY_PEER
		return self

	## The signal is emitted locally on the sender and replicated remotely.
	func call_local() -> SignalOptions:
		is_call_local = true
		return self

	## The signal is only replicated to remote peers and not emitted locally.
	func call_remote() -> SignalOptions:
		is_call_local = false
		return self


## Registers RPC config for the callable script method.
##
## Configuration is keyed per script, so it runs once regardless of how many
## instances call this in [method Object._init]. Component registration, by
## contrast, is per instance: a sub-node target injects itself into its
## entity's component table so entity RPCs can address it by a 1-byte id
## instead of a path string.
static func configure_rpc(callable: Callable) -> RpcOptions:
	var obj := callable.get_object()
	var method := callable.get_method()
	var script: Script = obj.get_script() if obj else null
	assert(script != null, "configure_rpc: Callable must be bound to an object with a script.")

	# Per-instance: register the target as an entity component. Runs even when
	# the config below is already cached for this script.
	if obj is Node:
		_inject_component_registration(obj as Node)

	var methods_map: Dictionary = _rpc_configs.get_or_add(script, {})
	if methods_map.has(method):
		return methods_map[method]

	var opt := RpcOptions.new()
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
	var methods_map: Dictionary = _rpc_configs.get(script, {})
	for method in methods_map:
		var opt: RpcOptions = methods_map[method]
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


## Allowlists [param property] on [param node] for [method sync_var] and returns
## its [Netw.VarOptions] to set the write policy.
##
## A property must be registered before a peer may write it, and the returned
## builder declares who may: [method Netw.VarOptions.authority] (default),
## [method Netw.VarOptions.controller], or [method Netw.VarOptions.any_peer]. Configuring a
## property a synchronizer already drives warns, since two writers is a
## double-authority mistake.
## [codeblock]
## func _init() -> void:
##     Netw.configure_var(self, &"fuel")                 # server writes, peers read
##     Netw.configure_var(self, &"steering").controller() # the controlling peer writes
## [/codeblock]
static func configure_var(node: Node, property: StringName) -> VarOptions:
	var script := node.get_script() as Script
	assert(script != null, "configure_var: Node must have a script.")
	assert(
		property in node,
		"configure_var: Property '%s' does not exist on '%s'."
		% [property, node.name]
	)
	_inject_var_lint(node)
	var props_map: Dictionary = _var_configs.get_or_add(script, {})
	if props_map.has(property):
		return props_map[property]
	var opt := VarOptions.new()
	props_map[property] = opt
	return opt


# Schedules the double-authority lint for a var-configured node once it is in
# the tree, where its entity's synchronizers are resolvable.
static func _inject_var_lint(node: Node) -> void:
	if node.has_meta(&"_netw_var_lint_injected"):
		return
	node.set_meta(&"_netw_var_lint_injected", true)
	if node.is_inside_tree():
		_lint_var_overlaps(node)
	else:
		node.tree_entered.connect(_lint_var_overlaps.bind(node), CONNECT_ONE_SHOT)


# Warns when a configured var is already driven by a synchronizer on the same
# entity, the same double-authority mistake SaveComponent lints. Best effort:
# a synchronizer that has not registered yet is simply not seen, never a false
# positive.
static func _lint_var_overlaps(node: Node) -> void:
	if not is_instance_valid(node):
		return
	var script := node.get_script() as Script
	if not script:
		return
	var configs: Dictionary = _var_configs.get(script, {})
	if configs.is_empty():
		return
	var entity := NetwEntity.of(node)
	if not entity or not is_instance_valid(entity.owner):
		return
	for property in configs:
		var real_path := entity.property_path(node, property)
		if not real_path.is_empty() and entity.governs_property(real_path):
			Netw.dbg.warn(
				"Netw.configure_var: '%s' on '%s' is already governed by a "
				+ "synchronizer; a variable and a synchronizer must not both "
				+ "write it (double authority)",
				[property, node.name],
			)


## Allowlists [param sig] for [method emit_entity_signal] and returns its
## [Netw.SignalOptions] to set the emit policy.
##
## A signal must be registered before a peer may drive its emission across the
## wire, and the returned builder declares who may, mirroring [method configure_var].
## [codeblock]
## func _init() -> void:
##     Netw.configure_signal(self.exploded)   # only the authority may broadcast it
## [/codeblock]
static func configure_signal(sig: Signal) -> SignalOptions:
	var node := sig.get_object() as Node
	assert(node != null, "configure_signal: Signal must be bound to a Node.")
	var signal_name := sig.get_name()
	var script := node.get_script() as Script
	assert(script != null, "configure_signal: Node must have a script.")
	var sigs_map: Dictionary = _signal_configs.get_or_add(script, {})
	if sigs_map.has(signal_name):
		return sigs_map[signal_name]
	var opt := SignalOptions.new()
	sigs_map[signal_name] = opt
	return opt


# ---------------------------------------------------------------------------
# Facade v2 — rpc, requests, var sync, signals verbs
# ---------------------------------------------------------------------------

## Calls [param callable]'s [code]@rpc[/code] method on every peer that currently
## sees the entity through [NetwInterest], with the given flat arguments.
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
	var relay := _resolve_relay(callable)
	if not relay:
		return
	relay.rpc_call(callable, args, peer_id)


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
	args: Array = []
) -> NetwPromise:
	assert(
		peer_id != 0,
		"request_id: peer_id cannot be 0; use request_all for broadcasts"
	)
	var relay := _resolve_relay(callable)
	if not relay:
		return null
	return relay.request_call(peer_id, callable, args)


## Sends a two-way request to the server and returns a [NetwPromise] that settles
## with the handler's return value.
static func request(callable: Callable, args: Array = []) -> NetwPromise:
	return request_id(1, callable, args)


## Broadcasts a two-way request to every peer that currently sees the entity
## through [NetwInterest] and returns a [NetwGroupPromise] that aggregates
## their replies.
##
## The awaited set is fixed at send time. See [NetwGroupPromise] for per-peer and
## all-done settling.
static func request_all(callable: Callable, args: Array = []) -> NetwGroupPromise:
	var relay := _resolve_relay(callable)
	if not relay:
		return null
	return relay.request_call_group(callable, args)


## Sends a two-way request to the entity's [member NetwEntity.controller] and
## returns a [NetwPromise] for its reply.
static func request_controller(callable: Callable, args: Array = []) -> NetwPromise:
	var node := callable.get_object() as Node
	var entity := NetwEntity.of(node)
	var controller := entity.controller if entity else 0
	if controller > 0:
		return request_id(controller, callable, args)
	return null


## Replicates the current value of [param property] on [param node] to its peers.
##
## The property must be allowlisted with [method configure_var], whose write
## policy decides whether this peer is allowed to push it. The server applies and
## rebroadcasts, a client requests the server.
## [codeblock]
## fuel -= 1
## Netw.sync_var(self, &"fuel")
## [/codeblock]
static func sync_var(node: Node, property: StringName, reliable: bool = true) -> void:
	var relay := RelayService.for_node(node)
	if relay:
		relay.send_var(node, property, reliable)


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
	var relay := RelayService.for_node(node)
	if not relay:
		return
	relay.send_signal(node, sig.get_name(), args)


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


static func _resolve_relay(callable: Callable) -> RelayService:
	var obj := callable.get_object()
	if not (obj is Node):
		Netw.dbg.error("Netw RPC target object must be a Node.")
		return null
	var node := obj as Node
	var entity := NetwEntity.of(node)
	if not entity:
		Netw.dbg.error("Netw RPC target node '%s' is not part of a NetwEntity.", [node.name])
		return null
	var relay := RelayService.for_node(node)
	if not relay:
		Netw.dbg.error("No RelayService found for node '%s'.", [node.name])
		return null
	return relay


## Opens the raw [NetwChannel] channel [param channel_id] for the tree enclosing
## [param node].
##
## [param channel_id] runs [code]100[/code] to [code]254[/code]. Use this for
## entity-less byte traffic such as voice or chat. See [NetwChannel].
static func channel(node: Node, channel_id: int) -> NetwChannel:
	var relay := RelayService.for_node(node)
	assert(
		relay != null,
		"Netw.channel: No RelayService found for the given node."
	)
	return NetwChannel.new(channel_id, relay)
