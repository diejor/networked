## Entity remote-call half of the replication core owned by [NetwMultiplayer]:
## [method Netw.rpc]/[method Netw.request]
## framing, transaction bookkeeping, and deferred-call parking for calls that
## arrive before their target's route is live.
##
## Mirrors [ReplicationCore]: engine-free [RefCounted], node-flavored
## operations (sending bytes, resolving [NetwMultiplayerCore], reading the clock)
## reach it through the owning [NetwMultiplayer], and outgoing sends
## ride [ReplicationCore]'s aggregation and component-addressing so
## RPC and replication share one wire format.
## [codeblock]
## rpc_interface.rpc_call(callable, args, peer_id)      # fire-and-forget
## rpc_interface.request_call(peer_id, callable, args)  # awaits a NetwPromise
## [/codeblock]
extends RefCounted

const RpcCore := preload("res://addons/networked/replication/rpc_core.gd")

## Default transaction timeout in seconds, used when a request does not pass its
## own. A request outstanding this long rejects with a timeout.
const REQUEST_TIMEOUT_SECONDS_DEFAULT := 5.0

# The owning NetwMultiplayer. A weakref because the owner holds this interface
# strongly and both are reference counted.
var _api_ref: WeakRef
var _replication: ReplicationCore

# Sender: a verb targeted a node that belongs to no NetwEntity.
var _sends_dropped_unroutable: int = 0
# Sender: the target entity has no live route on this peer (spawning or dead).
var _sends_dropped_not_live: int = 0

# Calls parked until the route they address binds.
var _park := NetwCallPark.new()

var _call_router: _CallRouter
var _txns: NetwTxnBook
# The settle waiting on each open transaction, a NetwPromise or a
# NetwGroupPromise, keyed the way NetwTxnBook keys the transaction itself.
var _settles: Dictionary[int, RefCounted] = { }


func _init(api: NetwMultiplayer, replication: ReplicationCore) -> void:
	_api_ref = weakref(api) if api else null
	_replication = replication
	_call_router = _CallRouter.new(self)
	_txns = NetwTxnBook.new()


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


# Resolves the tick engine, or null while no configurator has registered, so
# callers fall back to their no-clock path against the inert interface.
func _clock_interface() -> ClockCore:
	var api := _api()
	if api and api._clock.is_configured():
		return api._clock
	return null


# Returns the per-argument quantizer list configured for [param method] on
# [param node]'s script, or an empty array.
func _method_quantizers(node: Node, method: StringName) -> Array:
	if not node:
		return []
	var opt := NetwScriptModel.get_rpc_options(
		node.get_script() as Script,
		method,
	)
	return opt.quantizers if opt else []


## Parks a callback safely, respecting the per-sender backlog limit.
func defer_call(sender: int, route: int, cb: Callable) -> void:
	var api := _api()
	var clock := _clock_interface()
	var current := clock.tick if clock else (api._receive_tick() if api else 0)
	var timeout := int(clock.tickrate) if clock else 30
	var id := _park.park(sender, route, current + timeout)
	if id < 0 or not api:
		return
	api.when_live(
		route,
		func() -> void:
			if _park.resolve(id) and cb.is_valid():
				cb.call()
	)


## Sweeps expired deferrals.
func sweep_deferred_calls() -> void:
	var api := _api()
	var clock := _clock_interface()
	var current := clock.tick if clock else (api._receive_tick() if api else 0)
	_park.sweep(current)


## Sweeps expired transactions.
func sweep_transactions(current: int) -> void:
	if _txns == null:
		return
	for txn: int in _txns.expire(current):
		var settle: RefCounted = _settles.get(txn)
		_settles.erase(txn)
		if settle:
			settle.reject(ERR_TIMEOUT)


## Rejects every outstanding transaction whose reply can only come from
## [param peer_id], so a [method Netw.request] awaiting a peer that dropped
## fails at once instead of hanging until its timeout.
func handle_disconnect(peer_id: int) -> void:
	if _txns == null:
		return
	for txn: int in _txns.waiting_on(peer_id):
		var settle: RefCounted = _settles.get(txn)
		if settle is NetwPromise:
			settle.reject(ERR_UNAVAILABLE, "Peer disconnected")
			_close_txn(txn)
		elif settle is NetwGroupPromise:
			settle.remove_peer(peer_id)
			if settle.is_completed:
				_close_txn(txn)


## Drops all per-session state so no deferred call or transaction outlives its
## session.
func clear_session() -> void:
	_park.clear()
	# The book is null after dispose(), and teardown clears arrive deferred.
	if _txns == null:
		return
	var abandoned := _txns.drain()
	var settles := _settles
	_settles = { }
	for txn: int in abandoned:
		var settle: RefCounted = settles.get(txn)
		if settle:
			settle.reject(ERR_UNAVAILABLE, "Session ended")


## Breaks the mutual strong reference with the internal call router and drops
## the outstanding transactions so both can be released. Called from
## [method NetwEmbeddingHandle.dispose]. The interface is unusable afterward.
func dispose() -> void:
	_call_router = null
	_txns = null
	_settles.clear()


## Calls the RPC target on remote peers.
func rpc_call(callable: Callable, args: Array, peer_id: int) -> void:
	var api := _api()
	var native_core := api._native_core if api else null
	if not native_core:
		return
	var node := callable.get_object() as Node
	var entity := NetwEntity.of(node)
	if not entity:
		_sends_dropped_unroutable += 1
		return
	var route := native_core.liveness_route_of(entity)
	if route <= 0:
		_sends_dropped_not_live += 1
		if Netw.dbg.is_enabled():
			Netw.dbg.trace(
				"RpcCore: rpc target '%s' has no live route, dropped",
				[node.name],
			)
		return

	var target := _replication._resolve_comp(entity, node)
	var method := callable.get_method()
	var script := node.get_script() as Script

	var opt := NetwScriptModel.get_rpc_options(script, method)
	if not opt and (not script or not script.get_rpc_config().has(method)):
		Netw.dbg.warn(
			"RpcCore: method '%s' is not registered or annotated on '%s'.",
			[method, node.name],
			func(m): push_warning(m)
		)
		return

	var method_val := _encode_method_val(entity, node, method)
	var reliable := (
			NetwScriptModel.get_method_reliable(script, method)
			if script else true
	)
	var encoded_args := _encode_args(native_core, args)

	var local_id := api.get_unique_id()
	var local_targeted := peer_id == 0 or peer_id == local_id
	if local_targeted:
		if NetwScriptModel.get_method_call_local(script, method):
			# A call_local leg is a message from this peer to itself, so the
			# handler reads its own id through get_remote_sender_id, matching
			# native call_local. Save and restore keeps nested calls correct.
			var prev := api._relay_sender
			api._relay_sender = local_id
			callable.callv(args)
			api._relay_sender = prev
		elif peer_id == local_id:
			Netw.dbg.error(
				"RpcCore: cannot call local-only RPC "
				+ "on call_remote method '%s'",
				[method],
			)
			return

	var w := NetwBitBufferWriter.new()
	var flag := 0
	if peer_id == 0:
		flag |= 0
	elif peer_id == 1:
		flag |= 1
	else:
		flag |= 2

	w.put_aligned_u8(flag)
	if peer_id > 1:
		w.put_aligned_u32(peer_id)

	NetwScriptModel.write_call_body(
		w,
		method_val,
		encoded_args,
		_method_quantizers(node, method),
		NetwScriptModel.get_method_arg_types(script, method),
	)
	var payload := w.to_bytes()

	var recipients: Array[int] = []
	if peer_id == 0:
		recipients = api._replication.live_peers(entity)
	elif peer_id != local_id:
		recipients = [peer_id]

	for recipient in recipients:
		_replication.send_to(
			recipient,
			route,
			NetwFrameEnvelope.Channel.CALL,
			payload,
			reliable,
			target["comp"],
			target["path"],
		)


## Executes a request/promise call.
func request_call(
		peer_id: int,
		callable: Callable,
		args: Array,
		timeout_seconds: float = REQUEST_TIMEOUT_SECONDS_DEFAULT,
) -> NetwPromise:
	var api := _api()
	var native_core := api._native_core if api else null
	if not native_core:
		return null
	var node := callable.get_object() as Node
	var entity := NetwEntity.of(node)
	if not entity:
		_sends_dropped_unroutable += 1
		return null
	var route := native_core.liveness_route_of(entity)
	if route <= 0:
		_sends_dropped_not_live += 1
		if Netw.dbg.is_enabled():
			Netw.dbg.trace(
				"RpcCore: request target '%s' has no live route, dropped",
				[node.name],
			)
		return null

	var target := _replication._resolve_comp(entity, node)
	var method := callable.get_method()
	var script := node.get_script() as Script

	var opt := NetwScriptModel.get_rpc_options(script, method)
	if not opt and (not script or not script.get_rpc_config().has(method)):
		Netw.dbg.warn(
			"RpcCore: method '%s' is not registered or annotated on '%s'.",
			[method, node.name],
			func(m): push_warning(m)
		)
		return null

	var method_val := _encode_method_val(entity, node, method)
	var encoded_args := _encode_args(native_core, args)

	var txn := _txns.mint()
	var promise := NetwPromise.new()

	var clock := _clock_interface()
	var current := clock.tick if clock else api._receive_tick()
	var tickrate := int(clock.tickrate) if clock else 30
	var timeout := int(timeout_seconds * tickrate)
	_open_txn(txn, PackedInt64Array([peer_id]), promise, current + timeout)

	var w := NetwBitBufferWriter.new()
	var flag := 4
	if peer_id == 0:
		flag |= 0
	elif peer_id == 1:
		flag |= 1
	else:
		flag |= 2

	w.put_aligned_u8(flag)
	NetwCodec.put_varint(w, txn)
	if peer_id > 1:
		w.put_aligned_u32(peer_id)

	NetwScriptModel.write_call_body(
		w,
		method_val,
		encoded_args,
		_method_quantizers(node, method),
		NetwScriptModel.get_method_arg_types(node.get_script() as Script, method),
	)
	var payload := w.to_bytes()

	_replication.send_to(
		peer_id,
		route,
		NetwFrameEnvelope.Channel.CALL,
		payload,
		true,
		target["comp"],
		target["path"],
	)
	return promise


## Executes a request call group.
func request_call_group(
		callable: Callable,
		args: Array,
		timeout_seconds: float = REQUEST_TIMEOUT_SECONDS_DEFAULT,
) -> NetwGroupPromise:
	var api := _api()
	var native_core := api._native_core if api else null
	if not native_core:
		return null
	var node := callable.get_object() as Node
	var entity := NetwEntity.of(node)
	if not entity:
		_sends_dropped_unroutable += 1
		return null
	var route := native_core.liveness_route_of(entity)
	if route <= 0:
		_sends_dropped_not_live += 1
		if Netw.dbg.is_enabled():
			Netw.dbg.trace(
				"RpcCore: request target '%s' has no live route, dropped",
				[node.name],
			)
		return null

	var target := _replication._resolve_comp(entity, node)
	var method := callable.get_method()
	var script := node.get_script() as Script

	var opt := NetwScriptModel.get_rpc_options(script, method)
	if not opt and (not script or not script.get_rpc_config().has(method)):
		Netw.dbg.warn(
			"RpcCore: method '%s' is not registered or annotated on '%s'.",
			[method, node.name],
			func(m): push_warning(m)
		)
		return null

	var method_val := _encode_method_val(entity, node, method)
	var encoded_args := _encode_args(native_core, args)

	var peers := api._replication.live_peers(entity)
	var txn := _txns.mint()
	var promise := NetwGroupPromise.create(PackedInt32Array(peers))
	if peers.is_empty():
		api._settle_schedule(promise.resolve_all)

	var clock := _clock_interface()
	var current := clock.tick if clock else api._receive_tick()
	var tickrate := int(clock.tickrate) if clock else 30
	var timeout := int(timeout_seconds * tickrate)
	_open_txn(txn, PackedInt64Array(peers), promise, current + timeout)

	var w := NetwBitBufferWriter.new()
	var flag := 4
	w.put_aligned_u8(flag)
	NetwCodec.put_varint(w, txn)

	NetwScriptModel.write_call_body(
		w,
		method_val,
		encoded_args,
		_method_quantizers(node, method),
		NetwScriptModel.get_method_arg_types(node.get_script() as Script, method),
	)
	var payload := w.to_bytes()

	for peer_id in peers:
		_replication.send_to(
			peer_id,
			route,
			NetwFrameEnvelope.Channel.CALL,
			payload,
			true,
			target["comp"],
			target["path"],
		)
	return promise


## Sends a reply frame. A returned [Node] or [NetwEntity] crosses as a
## [NetwNodeRef] through the value codec, the same way a node argument does, so
## the requester resolves it to its own instance.
func send_reply(peer_id: int, route: int, txn: int, value: Variant) -> void:
	var w := NetwBitBufferWriter.new()
	NetwCodec.put_varint(w, txn)
	var api := _api()
	var encoded: Variant = _encode_arg(api._native_core, value) if api else value
	NetwCodec.write_values(w, [encoded], [], [])
	_replication.send_to(peer_id, route, NetwFrameEnvelope.Channel.REPLY, w.to_bytes(), true)


# Returns the wire method token: a 1-byte id when the component table is intact,
# otherwise the method name.
func _encode_method_val(
		entity: NetwEntity,
		node: Node,
		method: StringName,
) -> Variant:
	var script := node.get_script() as Script
	if script and not entity.components.poisoned:
		var mid := NetwScriptModel.get_method_id(script, method)
		if mid > 0:
			return mid
	return method


# Encodes each NetwEntity-bound node argument as a NetwNodeRef, leaving every
# other argument untouched. A node reference is its own codec kind, so a user
# value can never be mistaken for one and needs no escaping.
func _encode_args(native_core: NetwMultiplayerCore, args: Array) -> Array:
	var out: Array = []
	for arg in args:
		out.append(_encode_arg(native_core, arg))
	return out


func _encode_arg(native_core: NetwMultiplayerCore, arg: Variant) -> Variant:
	# A Node argument crosses by reference so the receiver rebinds it to its own
	# instance. The entity root carries just its route, and a child component
	# adds its addressing pair, so a whole entity or any node inside it can be
	# passed. A node in no entity has no route and rides through unchanged.
	var entity: NetwEntity = null
	var node: Node = null
	if arg is NetwEntity:
		entity = arg
		node = entity.owner
	elif arg is Node:
		node = arg
		entity = NetwEntity.of(arg)
	if entity == null:
		return arg
	var route := native_core.liveness_route_of(entity)
	if route <= 0:
		Netw.dbg.error("RpcCore.rpc_call: argument entity has no route")
		return null
	var target := _replication._resolve_comp(entity, node)
	return NetwNodeRef.create(route, int(target["comp"]), String(target["path"]))


## Handles an incoming [constant NetwFrameEnvelope.Channel.CALL] frame.
func handle_call(entity: NetwEntity, comp_node: Node, payload: PackedByteArray, sender: int) -> void:
	_call_router.handle_call(entity, comp_node, payload, sender)


func _open_txn(
		txn: int,
		addressed: PackedInt64Array,
		settle: RefCounted,
		deadline: int,
) -> void:
	_txns.open(txn, addressed, deadline)
	_settles[txn] = settle


func _close_txn(txn: int) -> void:
	_txns.close(txn)
	_settles.erase(txn)


## Handles an incoming [constant NetwFrameEnvelope.Channel.REPLY] frame.
func handle_reply(txn_id: int, sender: int, value: Variant) -> void:
	if _txns == null or not _txns.admits(txn_id, sender):
		return
	var settle: RefCounted = _settles.get(txn_id)
	if settle is NetwPromise:
		settle.resolve(value)
		_close_txn(txn_id)
	elif settle is NetwGroupPromise:
		settle.resolve_peer(sender, value)
		if settle.is_completed:
			_close_txn(txn_id)


## Returns this interface's contribution to [method NetwMultiplayer.stats_snapshot].
func counters() -> Dictionary:
	return {
		&"drops_backlog_limit": _park.refused(),
		&"sends_dropped_unroutable": _sends_dropped_unroutable,
		&"sends_dropped_not_live": _sends_dropped_not_live,
	}


class _CallRouter:
	extends RefCounted

	var _rpc: RpcCore


	func _init(rpc: RpcCore) -> void:
		_rpc = rpc


	func handle_call(
			entity: NetwEntity,
			comp_node: Node,
			payload: PackedByteArray,
			sender: int,
	) -> void:
		var r := NetwBitBufferReader.create(payload)
		var flag := r.get_aligned_u8()
		var target_type := flag & 3
		var has_txn := (flag & 4) != 0

		var txn := 0
		if has_txn:
			txn = NetwCodec.get_safe_varint(r)
			if txn < 0:
				return

		var target_peer := 0
		if target_type == 0:
			target_peer = 0
		elif target_type == 1:
			target_peer = 1
		elif target_type == 2:
			target_peer = r.get_aligned_u32()

		var script := comp_node.get_script() as Script
		if not script:
			return

		var method_raw: Variant = NetwScriptModel.read_method_token(r)
		var method: StringName = &""
		if method_raw is int:
			method = NetwScriptModel.get_method_name_by_id(script, method_raw)
		else:
			method = StringName(method_raw)

		if method.is_empty():
			return

		var opt := NetwScriptModel.get_rpc_options(script, method)
		if not opt and not script.get_rpc_config().has(method):
			Netw.dbg.warn(
				"RpcCore: method '%s' not registered "
				+ "or annotated on '%s'",
				[method, comp_node.name],
			)
			return

		var quantizers := opt.quantizers if opt else []
		var encoded_args: Array = NetwScriptModel.read_call_args(
			r,
			quantizers,
			NetwScriptModel.get_method_arg_types(script, method),
		)

		if not _is_sender_allowed(
			opt,
			script,
			method,
			entity,
			comp_node,
			sender,
		):
			Netw.dbg.warn(
				"RpcCore: unauthorized RPC sender %d "
				+ "for '%s' on '%s'",
				[sender, method, comp_node.name],
			)
			return

		if not NetwScriptModel.validate_argument_count(
			script,
			method,
			encoded_args.size(),
		):
			Netw.dbg.warn(
				"RpcCore: arity mismatch for '%s' on '%s'",
				[method, comp_node.name],
			)
			return

		var api: NetwMultiplayer = _rpc._api()
		if not api:
			return
		var native_core: NetwMultiplayerCore = api._native_core
		var args: Array = []
		for encoded in encoded_args:
			if encoded is NetwNodeRef:
				var ref: NetwNodeRef = encoded
				var arg_entity := native_core.wrapper_for_route(
					ref.route,
				) as NetwEntity
				if (
						arg_entity == null
						and native_core.liveness_route_state(ref.route)
						== NetwLivenessCore.STATE_UNKNOWN
				):
					api.when_live(
						ref.route,
						func() -> void:
							handle_call(entity, comp_node, payload, sender)
					)
					return

				if api.is_server() and arg_entity:
					if not api._replication.is_live_for(sender, arg_entity):
						api._warn_gate_verdict(
							ERR_UNAUTHORIZED,
							ref.route,
							"RpcCore: sender %d interest view "
							+ "does not admit route %d",
							[sender, ref.route],
						)
						return

				# Resolve to the node inside the entity, or null when the entity
				# is gone or the child has not spawned, the same graceful outcome
				# a freed node argument already produces.
				var arg_node: Node = null
				if arg_entity:
					arg_node = _rpc._replication.resolve_comp_node(
						arg_entity,
						ref.comp,
						ref.path,
					)
				args.append(arg_node)
			else:
				args.append(encoded)

		_record_interpolated_args(
			comp_node,
			args,
			opt.interpolators if opt else [],
		)

		var is_server_peer := api.is_server()

		# Relayed Request (Two-Way Client-to-Client Request)
		if txn > 0 and target_peer != 1 and is_server_peer and sender != 1:
			var promise := _rpc.request_call(
				target_peer,
				Callable(comp_node, method),
				args,
			)
			if promise:
				promise.then(
					func(val: Variant) -> void:
						_rpc.send_reply(sender, entity.route, txn, val)
				)
				promise.catch_error(
					func(_code: Error, _detail: String) -> void:
						_rpc.send_reply(sender, entity.route, txn, null)
				)
			return

		# Relayed Broadcast / Direct Call (One-Way Client-to-Client/All)
		var target := _rpc._replication._resolve_comp(entity, comp_node)
		var comp := target["comp"] as int
		var path := target["path"] as String
		var reliable := (
				NetwScriptModel.get_method_reliable(script, method)
				if script else true
		)

		if txn == 0 and target_peer != 1 and is_server_peer and sender != 1:
			if target_peer == 0:
				var recipients := _rpc._replication.live_peers(entity)
				recipients.erase(sender)
				for recipient in recipients:
					_rpc._replication.send_to(
						recipient,
						entity.route,
						NetwFrameEnvelope.Channel.CALL,
						payload,
						reliable,
						comp,
						path,
					)
			else:
				if _rpc._replication.is_live_for(target_peer, entity):
					_rpc._replication.send_to(
						target_peer,
						entity.route,
						NetwFrameEnvelope.Channel.CALL,
						payload,
						reliable,
						comp,
						path,
					)
			# Do not execute locally on the Server if it's client-to-client only
			if target_peer != 0:
				return

		# Execute locally
		if opt and not opt.defer_signal_name.is_empty():
			if not Netw._defer_signal_fired(comp_node, opt.defer_signal_name):
				if comp_node.has_signal(opt.defer_signal_name):
					comp_node.connect(
						opt.defer_signal_name,
						func() -> void:
							_execute_call(
								entity,
								comp_node,
								method,
								args,
								txn,
								sender,
							),
						CONNECT_ONE_SHOT,
					)
					return

		_execute_call(entity, comp_node, method, args, txn, sender)


	func _execute_call(
			entity: NetwEntity,
			comp_node: Node,
			method: StringName,
			args: Array,
			txn: int,
			sender: int,
	) -> void:
		var result = comp_node.callv(method, args)
		if txn > 0:
			if result is NetwPromise:
				result.then(
					func(val: Variant) -> void:
						_rpc.send_reply(sender, entity.route, txn, val)
				)
				result.catch_error(
					func(_code: Error, _detail: String) -> void:
						_rpc.send_reply(sender, entity.route, txn, null)
				)
			else:
				_rpc.send_reply(sender, entity.route, txn, result)


	func _record_interpolated_args(
			comp_node: Node,
			args: Array,
			interpolators: Array,
	) -> void:
		if interpolators.is_empty():
			return
		var api := _rpc._api()
		var iface := api._display if api else null
		if not iface:
			return
		var tick := api._receive_tick()
		for i in interpolators.size():
			var spec := interpolators[i] as NetwInterpolate
			if not spec or spec.target.is_empty():
				continue
			if i >= args.size():
				continue
			iface._record(
				comp_node,
				spec.target,
				args[i],
				tick,
				spec,
				false,
			)


	func _is_sender_allowed(
			opt: NetwScriptModel.SyncConfig,
			script: Script,
			method: StringName,
			entity: NetwEntity,
			node: Node,
			sender: int,
	) -> bool:
		if sender == 1:
			return true
		if opt and opt.is_controller_only:
			return entity != null and sender == entity.controller
		var rpc_mode = NetwScriptModel.get_method_rpc_mode(script, method)
		if rpc_mode == 2:
			return sender == node.get_multiplayer_authority()
		elif rpc_mode == 1:
			return true
		return false
