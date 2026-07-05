## Session-wide carrier that routes entity traffic by route id, so a packet never
## targets a node that has not spawned yet.
##
## A Godot RPC is addressed to a node, so it raises an error when the receiver has
## not spawned that node, and the spawn and despawn edges make that ordering
## impossible to guarantee. RelayService is mounted on the [MultiplayerTree]
## before any entity exists, so a carrier always has a valid target. Resolution
## happens afterward in Networked's own vocabulary: [LivenessService] maps a
## frame's route to a [NetwEntity], and a frame that cannot be resolved is counted
## and dropped or briefly deferred, never raised. Games rarely touch this service
## directly. They call the [Netw] facade, which encodes arguments and resolves the
## relay for a node.
##
## [br][br][b]The frame[/b]
## [br]Every payload crosses the wire in one compact envelope. The component byte
## selects the target within the entity: [code]0[/code] is the entity root,
## [code]1[/code] to [code]254[/code] a node in the entity's hydrated component
## table, [code]255[/code] a relative path string for an unmapped node. The length
## prefix lets many frames pack into a single datagram, which is what per-peer
## aggregation flushes once per tick.
## [codeblock]
## frame:    [ route varint | comp u8 | command u8 | len varint | payload ]
## datagram: [ frame ][ frame ][ frame ] ...   one carrier packet per peer per tick
## [/codeblock]
##
## [br][b]Resolve, gate, dispatch[/b]
## [br]An arriving frame resolves its route through [LivenessService], and the
## route's [enum LivenessService.State] decides its fate. Absence is normal while a
## spawn packet is still in flight, so it is a drop or a short deferral, never an
## error.
## [codeblock]
## route state                    arriving frame
##   LIVE      ─▶ dispatch to the resolved node
##   UNKNOWN   ─▶ reliable CALL defers via when_live, otherwise drop (counted)
##   LINGERING ─▶ drop (counted)
##   DEAD      ─▶ drop (counted)
## [/codeblock]
##
## [b]Command families[/b]
## [br][enum Command] multiplexes the one carrier pair. [constant Command.STATE]
## and [constant Command.INPUT] are the per-tick synchronizer carriers, pumped
## together every [signal MultiplayerClock.after_tick]. [constant Command.CALL] and
## [constant Command.REPLY] carry entity RPCs and their transaction replies.
## [constant Command.VAR_SYNC] and [constant Command.SIGNAL] carry on-demand
## variable and signal replication. [constant Command.ACTION] carries [NetwAction]
## traffic. Ids [code]100[/code] to [code]254[/code] are user channels over
## [NetwChannel].
##
## [br][br][b]Authority[/b]
## [br]A discrete command validates its sender on the receiver against the target's
## own script, never against anything on the wire. An entity call checks the
## method's [code]@rpc[/code] mode, a variable or signal checks its registered
## write policy, and the server is always trusted. A frame from an unauthorized
## sender is dropped and counted.
##
## [br][br][b]Observability[/b]
## [br]The drop-if-absent contract means a spawn or despawn edge produces healthy
## drops. [method monitor_snapshot] surfaces per-reason drop and packet counters so
## a gap the next snapshot heals stays visible instead of assumed.
class_name RelayService
extends NetwService

## Payload families multiplexed over the one carrier pair. The command byte in the
## frame names which handler a payload reaches on the receiver.
enum Command {
	## Server-authoritative state carrier from a [StateSynchronizer].
	STATE = 0,
	## Controller-authoritative input carrier from an [InputSynchronizer].
	INPUT = 1,
	## Routed [NetwAction] traffic.
	ACTION = 2,
	## Entity remote procedure call sent from a peer, from [method Netw.rpc].
	CALL = 3,
	## Transaction reply returning the value of a [method Netw.request].
	REPLY = 4,
	## On-demand signal replication from [method Netw.emit_entity_signal].
	SIGNAL = 5,
	## On-demand variable replication from [method Netw.sync_var].
	VAR_SYNC = 6,
}

const _MAX_CLOCK_BIND_ATTEMPTS := 60

## Default transaction timeout in seconds, used when a request does not pass its
## own. A request outstanding this long rejects with a timeout.
const REQUEST_TIMEOUT_SECONDS_DEFAULT := 5.0

# Static script configuration mappings (cached)
static var _script_methods: Dictionary = {}

var _handlers: Dictionary[int, Callable] = {}
var _defer_channels: Dictionary[int, bool] = {}
var _senders: Array[PackedSynchronizer] = []
var _clock_connected := false
var _clock_bind_attempts: int = 0

var _drops_unknown_route: int = 0
var _drops_not_live: int = 0
var _drops_no_node: int = 0
var _drops_backlog_limit: int = 0

# Aggregation packet counters
var _sent_packets: int = 0
var _sent_bytes: int = 0
var _received_packets: int = 0
var _received_bytes: int = 0

# Deferral/backlog tracking
var _deferred_calls: Array[Dictionary] = []
var _frame_counter: int = 0

# Sub-routers
var _call_router: _CallRouter
var _txn_book: _TxnBook
var _var_signal_router: _VarSignalRouter

var _unreliable_buffers: Dictionary = {} # peer_id -> PackedByteArray
var _reliable_buffers: Dictionary = {} # peer_id -> PackedByteArray

var liveness: LivenessService:
	get:
		return LivenessService.for_node(self)


## Resolves the [RelayService] for the [MultiplayerTree] enclosing [param node],
## or [code]null[/code] when there is none.
static func for_node(node: Node) -> RelayService:
	var mt := MultiplayerTree.resolve(node)
	if not mt:
		return null
	var relay := mt.get_service(RelayService) as RelayService
	if relay:
		return relay
	return mt.find_service_node(RelayService) as RelayService


## Packaging helper for unit/integration tests to frame envelopes.
static func test_pack_frame(route: int, comp: int, command: int, payload: PackedByteArray, path: String = "") -> PackedByteArray:
	return _Envelope.pack(route, comp, command, payload, path)


## Unpacking helper for unit/integration tests to unpack envelopes from bytes.
static func test_unpack_all_frames(bytes: PackedByteArray) -> Array:
	var list := _Envelope.unpack_all(bytes)
	var out := []
	for env in list:
		out.append({
			"route": env["route"],
			"comp": env["comp"],
			"command": env["command"],
			"payload": env["payload"],
			"path": env["path"]
		})
	return out


## Resolves the RPC options (deferral, policy) for [param method] on [param script].
static func get_rpc_options(script: Script, method: StringName) -> Netw.RpcOptions:
	var methods_map = Netw._rpc_configs.get(script)
	if methods_map and methods_map.has(method):
		return methods_map[method]
	return null


## Extracts the native rpc_mode for [param method] on [param script] from the @rpc annotation config.
static func get_method_rpc_mode(script: Script, method: StringName) -> int:
	var rpc_config = script.get_rpc_config()
	if rpc_config and rpc_config.has(method):
		var info = rpc_config[method]
		if info is Dictionary and info.has("rpc_mode"):
			return info["rpc_mode"]
	return 0


## Returns [code]true[/code] when [param method] on [param script] is annotated
## with a reliable transfer mode. Godot's [code]transfer_mode[/code] is
## [code]2[/code] for reliable and [code]0[/code]/[code]1[/code] for the
## unreliable variants. Methods with no [code]@rpc[/code] config default to
## reliable, the safe choice for discrete calls.
static func get_method_reliable(script: Script, method: StringName) -> bool:
	var rpc_config = script.get_rpc_config()
	if rpc_config and rpc_config.has(method):
		var info = rpc_config[method]
		if info is Dictionary and info.has("transfer_mode"):
			return int(info["transfer_mode"]) == 2
	return true


static func get_method_call_local(script: Script, method: StringName) -> bool:
	var rpc_config = script.get_rpc_config()
	if rpc_config and rpc_config.has(method):
		var info = rpc_config[method]
		if info is Dictionary and info.has("call_local"):
			return bool(info["call_local"])
	return false


## Validates if the argument count matches the script signature.
static func validate_argument_count(script: Script, method: StringName, arg_count: int) -> bool:
	var s := script
	while s != null:
		for m in s.get_script_method_list():
			if m["name"] == method:
				var total_args: int = m["args"].size()
				var default_count := 0
				if m.has("default_args"):
					default_count = m["default_args"].size()
				var min_args := total_args - default_count
				return arg_count >= min_args and arg_count <= total_args
		s = s.get_base_script()
	return false


## Helper to assign and retrieve 1-byte method IDs per script.
static func get_method_id(script: Script, method: StringName) -> int:
	var list := _get_sorted_methods(script)
	var idx := list.find(method)
	if idx >= 0:
		return idx + 1
	return 0


## Helper to retrieve the method name from its 1-byte ID.
static func get_method_name_by_id(script: Script, method_id: int) -> StringName:
	var list := _get_sorted_methods(script)
	var idx := method_id - 1
	if idx >= 0 and idx < list.size():
		return list[idx]
	return &""


## Returns the declared parameter [enum Variant.Type] list for [param method]
## on [param script], used to reconstruct quantized arguments on the receiver.
## An untyped parameter reports [constant TYPE_NIL].
static func get_method_arg_types(script: Script, method: StringName) -> Array:
	var s := script
	while s != null:
		for m in s.get_script_method_list():
			if m["name"] == method:
				var out: Array = []
				for a in m["args"]:
					out.append(int(a.get("type", TYPE_NIL)))
				return out
		s = s.get_base_script()
	return []


# Writes a CALL body: method token, then a length-tagged, per-argument stream.
# Each argument is prefixed by a flag byte, 1 when the value is bit-packed by
# its quantizer and 0 when it rides NetwCodec's tagged fallback. The flag keeps
# encoder and decoder structurally in step even when an argument at a quantized
# slot turns out to be a route marker or other non-scalar.
static func _write_call_body(
		w: NetwBitBuffer.Writer,
		method_val: Variant,
		encoded_args: Array,
		quantizers: Array,
		arg_types: Array,
) -> void:
	if method_val is int:
		w.put_aligned_u8(1)
		w.put_aligned_u8(int(method_val))
	else:
		w.put_aligned_u8(0)
		var nb := String(method_val).to_utf8_buffer()
		w.put_aligned_u32(nb.size())
		w.put_aligned_bytes(nb)

	w.put_aligned_u8(encoded_args.size())
	for i in encoded_args.size():
		var q: NetwQuantize = quantizers[i] if i < quantizers.size() else null
		var t: int = arg_types[i] if i < arg_types.size() else TYPE_NIL
		# Quantize only when the quantizer accepts both the declared parameter
		# type (needed to reconstruct on decode) and the actual value. The
		# quantizer owns that decision, so new supported types just work.
		var quantized := q != null and q.supports_type(t) \
				and q.supports_type(typeof(encoded_args[i]) as Variant.Type)
		if quantized:
			w.put_aligned_u8(1)
			NetwCodec.encode_value(w, encoded_args[i], q)
		else:
			w.put_aligned_u8(0)
			NetwCodec.encode_value(w, encoded_args[i], null)


# Reads the method token written by [method _write_call_body], returning the
# 1-byte id as an int or the method name as a StringName.
static func _read_method_token(r: NetwBitBuffer.Reader) -> Variant:
	var flag := r.get_aligned_u8()
	if flag == 1:
		return r.get_aligned_u8()
	var name_len := r.get_aligned_u32()
	return StringName(r.get_aligned_bytes(name_len).get_string_from_utf8())


# Reads the per-argument stream written by [method _write_call_body]. Quantized
# arguments reconstruct through their declared parameter type.
static func _read_call_args(
		r: NetwBitBuffer.Reader,
		quantizers: Array,
		arg_types: Array,
) -> Array:
	var count := r.get_aligned_u8()
	var out: Array = []
	for i in count:
		var flag := r.get_aligned_u8()
		if flag == 1:
			var q: NetwQuantize = quantizers[i] if i < quantizers.size() else null
			var t: int = arg_types[i] if i < arg_types.size() else TYPE_NIL
			out.append(NetwCodec.decode_value(r, t, q))
		else:
			out.append(NetwCodec.decode_value(r, TYPE_NIL, null))
	return out


# Returns the per-argument quantizer list configured for [param method] on
# [param node]'s script, or an empty array.
func _method_quantizers(node: Node, method: StringName) -> Array:
	return _method_quantizers_for(node.get_script() as Script, method)


static func _method_quantizers_for(script: Script, method: StringName) -> Array:
	if not script:
		return []
	var opt := get_rpc_options(script, method)
	return opt.quantizers if opt else []


static func _get_sorted_methods(script: Script) -> Array:
	if _script_methods.has(script):
		return _script_methods[script]
	var methods: Array = []
	var rpc_config = script.get_rpc_config()
	if rpc_config:
		for m in rpc_config:
			methods.append(StringName(m))
	methods.sort()
	_script_methods[script] = methods
	return methods


func _init() -> void:
	_call_router = _CallRouter.new(self)
	_txn_book = _TxnBook.new(self)
	_var_signal_router = _VarSignalRouter.new(self)


func _process(_delta: float) -> void:
	_frame_counter += 1
	sweep_deferred_calls()
	
	# Sweep transaction timeouts using ticks/frame count
	var clock := MultiplayerClock.for_node(self)
	var current := clock.tick if clock else _frame_counter
	_txn_book.sweep(current)


## Registers [param handler] to receive payloads for [param channel].
##
## [param handler] is called as:
## [code]handler(entity: NetwEntity, payload: PackedByteArray, sender: int)[/code].
## If [param defer_when_unknown] is set to [code]true[/code], incoming packets for this channel 
## targeting unknown routes will be deferred until the route transitions to LIVE.
func register_channel(
		channel: int,
		handler: Callable,
		defer_when_unknown: bool = false,
) -> void:
	_handlers[channel] = handler
	if defer_when_unknown:
		_defer_channels[channel] = true
	else:
		_defer_channels.erase(channel)


## Registers [param sync] as a sender on the tick pump.
func register_sender(sync: PackedSynchronizer) -> void:
	if not _senders.has(sync):
		_senders.append(sync)
	_ensure_clock_connection()


## Unregisters [param sync] from the tick pump. Idempotent.
func unregister_sender(sync: PackedSynchronizer) -> void:
	_senders.erase(sync)


## Sends [param payload] to [param peer_id].
func send_to(
		peer_id: int,
		route: int,
		command: int,
		payload: PackedByteArray,
		reliable: bool,
		comp: int = 0,
		path: String = "",
		batched: bool = false,
) -> void:
	if route < 0:
		return
	if multiplayer and multiplayer.multiplayer_peer \
			and peer_id == multiplayer.get_unique_id():
		_dispatch(route, comp, command, payload, path, peer_id, reliable)
		return

	var framed := _Envelope.pack(route, comp, command, payload, path)
	# Aggregation is flushed by the tick pump. Without a running clock there is
	# no flush, so a frame would sit buffered forever. Only batch when the pump
	# is live, otherwise send immediately. Per-tick carriers batch by default;
	# a custom command opts in through [param batched].
	var should_aggregate := _clock_connected and ( \
			batched \
			or command in [Command.STATE, Command.INPUT, Command.VAR_SYNC, Command.SIGNAL])

	if should_aggregate:
		var buffers = _reliable_buffers if reliable else _unreliable_buffers
		var buf: PackedByteArray = buffers.get_or_add(peer_id, PackedByteArray())
		if not reliable:
			var max_size := 1200
			var api := multiplayer as SceneMultiplayer
			if api:
				max_size = maxi(128, api.max_sync_packet_size - 150)
			if buf.size() + framed.size() > max_size:
				_flush_buffer(peer_id, false)
				buf = _unreliable_buffers.get_or_add(peer_id, PackedByteArray())
		buf.append_array(framed)
		buffers[peer_id] = buf
	else:
		_send_packet(peer_id, framed, reliable)


func _send_packet(peer_id: int, bytes: PackedByteArray, reliable: bool) -> void:
	if bytes.is_empty():
		return
	_sent_packets += 1
	_sent_bytes += bytes.size()
	if reliable:
		_carrier_reliable.rpc_id(peer_id, bytes)
	else:
		_carrier_unreliable.rpc_id(peer_id, bytes)


## Flushes all buffered aggregates.
func flush_all_buffers() -> void:
	for peer_id in _unreliable_buffers.duplicate():
		_flush_buffer(peer_id, false)
	_unreliable_buffers.clear()
	for peer_id in _reliable_buffers.duplicate():
		_flush_buffer(peer_id, true)
	_reliable_buffers.clear()


func _flush_buffer(peer_id: int, reliable: bool) -> void:
	var buffers = _reliable_buffers if reliable else _unreliable_buffers
	var buf: PackedByteArray = buffers.get(peer_id, PackedByteArray())
	if buf.is_empty():
		return
	_send_packet(peer_id, buf, reliable)
	buffers.erase(peer_id)


## Parks a callback safely, respecting the per-sender backlog limit.
func defer_call(sender: int, route: int, cb: Callable) -> void:
	var active_count := 0
	for d in _deferred_calls:
		if d.sender == sender:
			active_count += 1
	if active_count >= 100:
		_drops_backlog_limit += 1
		return

	var clock := MultiplayerClock.for_node(self)
	var current := clock.tick if clock else _frame_counter
	var timeout := int(clock.tickrate) if clock else 30
	var deadline := current + timeout

	var info := {
		"sender": sender,
		"deadline": deadline,
		"route": route,
		"active": true
	}
	_deferred_calls.append(info)

	liveness.when_live(route, func() -> void:
		if info.active:
			info.active = false
			if cb.is_valid():
				cb.call()
	)


## Sweeps expired deferrals.
func sweep_deferred_calls() -> void:
	var clock := MultiplayerClock.for_node(self)
	var current := clock.tick if clock else _frame_counter
	var remaining: Array[Dictionary] = []
	for d in _deferred_calls:
		if d.active and current < d.deadline:
			remaining.append(d)
	_deferred_calls = remaining


## Calls the RPC target on remote peers.
func rpc_call(callable: Callable, args: Array, peer_id: int) -> void:
	var liveness := LivenessService.for_node(self)
	if not liveness:
		return
	var node := callable.get_object() as Node
	var entity := NetwEntity.of(node)
	if not entity:
		return
	var route := liveness.route_of(entity)
	if route <= 0:
		return

	var target := _resolve_comp(entity, node)
	var method := callable.get_method()
	var script := node.get_script() as Script

	var opt := get_rpc_options(script, method)
	if not opt and (not script or not script.get_rpc_config().has(method)):
		Netw.dbg.warn(
			"RelayService: method '%s' is not registered or annotated on '%s'.",
			[method, node.name],
			func(m): push_warning(m)
		)
		return

	var method_val := _encode_method_val(entity, node, method)
	var reliable := get_method_reliable(script, method) if script else true
	var encoded_args := _encode_args(liveness, args)

	var local_id := multiplayer.get_unique_id() if multiplayer else 0
	var local_targeted := peer_id == 0 or peer_id == local_id
	if local_targeted:
		if get_method_call_local(script, method):
			callable.callv(args)
		elif peer_id == local_id:
			Netw.dbg.error(
				"RelayService: cannot call local-only RPC on call_remote method '%s'",
				[method]
			)
			return

	var w := NetwBitBuffer.Writer.new()
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

	_write_call_body(
		w, method_val, encoded_args,
		_method_quantizers(node, method),
		get_method_arg_types(script, method),
	)
	var payload := w.to_bytes()

	var recipients: Array[int] = []
	if peer_id == 0:
		recipients = liveness.live_peers(entity)
	elif peer_id != local_id:
		recipients = [peer_id]

	for recipient in recipients:
		send_to(
			recipient, route, Command.CALL, payload, reliable,
			target["comp"], target["path"]
		)


## Executes a request/promise call.
func request_call(
		peer_id: int,
		callable: Callable,
		args: Array,
		timeout_seconds: float = REQUEST_TIMEOUT_SECONDS_DEFAULT,
) -> NetwPromise:
	var liveness := LivenessService.for_node(self)
	if not liveness:
		return null
	var node := callable.get_object() as Node
	var entity := NetwEntity.of(node)
	if not entity:
		return null
	var route := liveness.route_of(entity)
	if route <= 0:
		return null

	var target := _resolve_comp(entity, node)
	var method := callable.get_method()
	var script := node.get_script() as Script

	var opt := get_rpc_options(script, method)
	if not opt and (not script or not script.get_rpc_config().has(method)):
		Netw.dbg.warn(
			"RelayService: method '%s' is not registered or annotated on '%s'.",
			[method, node.name],
			func(m): push_warning(m)
		)
		return null

	var method_val := _encode_method_val(entity, node, method)
	var encoded_args := _encode_args(liveness, args)

	var txn := _txn_book.make_id()
	var promise := NetwPromise.new()

	var clock := MultiplayerClock.for_node(self)
	var current := clock.tick if clock else _frame_counter
	var tickrate := int(clock.tickrate) if clock else 30
	var timeout := int(timeout_seconds * tickrate)
	_txn_book.register(txn, promise, peer_id, current + timeout)

	var w := NetwBitBuffer.Writer.new()
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

	_write_call_body(
		w, method_val, encoded_args,
		_method_quantizers(node, method),
		get_method_arg_types(node.get_script() as Script, method),
	)
	var payload := w.to_bytes()

	send_to(
		peer_id, route, Command.CALL, payload, true,
		target["comp"], target["path"]
	)
	return promise


## Executes a request call group.
func request_call_group(
		callable: Callable,
		args: Array,
		timeout_seconds: float = REQUEST_TIMEOUT_SECONDS_DEFAULT,
) -> NetwGroupPromise:
	var liveness := LivenessService.for_node(self)
	if not liveness:
		return null
	var node := callable.get_object() as Node
	var entity := NetwEntity.of(node)
	if not entity:
		return null
	var route := liveness.route_of(entity)
	if route <= 0:
		return null

	var target := _resolve_comp(entity, node)
	var method := callable.get_method()
	var script := node.get_script() as Script

	var opt := get_rpc_options(script, method)
	if not opt and (not script or not script.get_rpc_config().has(method)):
		Netw.dbg.warn(
			"RelayService: method '%s' is not registered or annotated on '%s'.",
			[method, node.name],
			func(m): push_warning(m)
		)
		return null

	var method_val := _encode_method_val(entity, node, method)
	var encoded_args := _encode_args(liveness, args)

	var peers := liveness.live_peers(entity)
	var txn := _txn_book.make_id()
	var promise := NetwGroupPromise.new(peers)

	var clock := MultiplayerClock.for_node(self)
	var current := clock.tick if clock else _frame_counter
	var tickrate := int(clock.tickrate) if clock else 30
	var timeout := int(timeout_seconds * tickrate)
	_txn_book.register(txn, promise, peers, current + timeout)

	var w := NetwBitBuffer.Writer.new()
	var flag := 4
	w.put_aligned_u8(flag)
	NetwCodec.put_varint(w, txn)

	_write_call_body(
		w, method_val, encoded_args,
		_method_quantizers(node, method),
		get_method_arg_types(node.get_script() as Script, method),
	)
	var payload := w.to_bytes()

	for peer_id in peers:
		send_to(
			peer_id, route, Command.CALL, payload, true,
			target["comp"], target["path"]
		)
	return promise


## Sends an on-demand variable sync for [param property] on [param node].
##
## The server broadcasts to every live peer, a client requests the server. The
## server validates write authority before applying and rebroadcasting.
func send_var(node: Node, property: StringName, reliable: bool = true) -> void:
	var frame := _resolve_send(node)
	if frame.is_empty():
		var entity := NetwEntity.of(node)
		if not entity and not node.has_meta(&"_netw_unroutable_warned"):
			node.set_meta(&"_netw_unroutable_warned", true)
			Netw.dbg.warn(
				"sync_var: Node '%s' is not part of any NetwEntity.",
				[node.name],
				func(m): push_warning(m)
			)
		return

	var script := node.get_script() as Script
	var configs: Dictionary = Netw._var_configs.get(script, {}) if script else {}
	if not configs.has(property):
		Netw.dbg.warn(
			"sync_var: Property '%s' is not registered on '%s'.",
			[property, node.name],
			func(m): push_warning(m)
		)
		return

	if not (property in node):
		Netw.dbg.warn(
			"sync_var: Property '%s' does not exist on '%s'.",
			[property, node.name],
			func(m): push_warning(m)
		)
		return

	var payload := var_to_bytes([property, node.get(property)])
	_send_entity_event(frame, Command.VAR_SYNC, payload, reliable)


## Sends a networked emission of [param signal_name] on [param node] with
## [param args]. Same routing and authority rules as [method send_var].
func send_signal(
		node: Node,
		signal_name: StringName,
		args: Array,
		reliable: bool = true,
) -> void:
	var frame := _resolve_send(node)
	if frame.is_empty():
		var entity := NetwEntity.of(node)
		if not entity and not node.has_meta(&"_netw_unroutable_warned"):
			node.set_meta(&"_netw_unroutable_warned", true)
			Netw.dbg.warn(
				"send_signal: Node '%s' is not part of any NetwEntity.",
				[node.name],
				func(m): push_warning(m)
			)
		return

	var script := node.get_script() as Script
	var is_call_local := true
	if script:
		var configs: Dictionary = Netw._signal_configs.get(script, {})
		if not configs.has(signal_name):
			Netw.dbg.warn(
				"send_signal: Signal '%s' is not registered on '%s'.",
				[signal_name, node.name],
				func(m): push_warning(m)
			)
			return
		is_call_local = configs[signal_name].is_call_local

	if is_call_local:
		var callable := Callable(node, &"emit_signal")
		callable.callv([signal_name] + args)

	var payload := var_to_bytes([signal_name, args])
	_send_entity_event(frame, Command.SIGNAL, payload, reliable)


# Resolves the entity, route, and component addressing for an on-demand event
# send. Returns an empty dictionary when the node is unroutable.
func _resolve_send(node: Node) -> Dictionary:
	var entity := NetwEntity.of(node)
	if not entity:
		return {}
	var liveness := LivenessService.for_node(self)
	if not liveness:
		return {}
	var route := liveness.route_of(entity)
	if route <= 0:
		return {}
	var target := _resolve_comp(entity, node)
	return {
		"entity": entity,
		"route": route,
		"comp": target["comp"],
		"path": target["path"],
	}


func _send_entity_event(
		frame: Dictionary,
		command: int,
		payload: PackedByteArray,
		reliable: bool,
) -> void:
	var mt := _tree()
	if mt and mt.is_host:
		var liveness := LivenessService.for_node(self)
		for recipient in liveness.live_peers(frame["entity"]):
			send_to(recipient, frame["route"], command, payload, reliable, frame["comp"], frame["path"])
	else:
		var local_id := multiplayer.get_unique_id() if multiplayer else 0
		if local_id != 1:
			send_to(
				1, frame["route"], command, payload, reliable,
				frame["comp"], frame["path"]
			)


## Sends a reply frame.
func send_reply(peer_id: int, route: int, txn: int, value: Variant) -> void:
	var w := NetwBitBuffer.Writer.new()
	NetwCodec.put_varint(w, txn)
	w.put_aligned_bytes(var_to_bytes(value))
	var payload := w.to_bytes()
	send_to(peer_id, route, Command.REPLY, payload, true)


# Resolves the component addressing for a target node under its entity root.
# Returns the 1-byte component id and, when the table cannot compress it, the
# fallback relative path string. comp 0 is the entity root.
func _resolve_comp(entity: NetwEntity, node: Node) -> Dictionary:
	if node == entity.owner:
		return { "comp": 0, "path": "" }
	var rel := entity.relative_path(entity.owner, node)
	if entity._table_poisoned or not entity._ids_by_path.has(rel):
		return { "comp": 255, "path": String(rel) }
	return { "comp": entity._ids_by_path[rel], "path": "" }


# Returns the wire method token: a 1-byte id when the component table is intact,
# otherwise the method name.
func _encode_method_val(entity: NetwEntity, node: Node, method: StringName) -> Variant:
	var script := node.get_script() as Script
	if script and not entity._table_poisoned:
		var mid := get_method_id(script, method)
		if mid > 0:
			return mid
	return method


# Escapes any user dictionary that collides with the route-marker shape, then
# encodes NetwEntity-bound node arguments as their route.
func _encode_args(liveness: LivenessService, args: Array) -> Array:
	var out: Array = []
	for arg in args:
		var escaped_arg = arg
		if arg is Dictionary and arg.has("__netw_route"):
			escaped_arg = arg.duplicate()
			escaped_arg["__escaped_netw_route"] = escaped_arg["__netw_route"]
			escaped_arg.erase("__netw_route")
		out.append(_encode_arg(liveness, escaped_arg))
	return out


func _encode_arg(liveness: LivenessService, arg: Variant) -> Variant:
	var entity: NetwEntity = null
	if arg is NetwEntity:
		entity = arg
	elif arg is Node:
		entity = NetwEntity.of(arg)
		if entity and entity.owner != arg:
			Netw.dbg.error(
				"RelayService.rpc_call: node '%s' is not an entity root; pass the root or a plain Variant",
				[str((arg as Node).name)]
			)
			return null
	if entity == null:
		return arg
	var route := liveness.route_of(entity)
	if route <= 0:
		Netw.dbg.error("RelayService.rpc_call: argument entity has no route")
		return null
	return { "__netw_route": route }


## Returns drop and occupancy counters for the debug monitor.
##
## A nonzero drop count across a spawn or despawn edge is the healthy outcome
## of the drop-if-absent contract. Sustained growth in steady state means a
## route never became [constant LivenessService.State.LIVE] on the receiver.
## [codeblock]
## {
##   ┠╴ drops_unknown_route: int   # no binding on this peer yet
##   ┠╴ drops_not_live: int        # route known but LINGERING or DEAD
##   ┠╴ drops_no_node: int         # binding exists, owner node freed
##   ┠╴ drops_backlog_limit: int   # messages dropped due to buffer limit
##   ┠╴ active_senders: int        # synchronizers on the tick pump
##   ┠╴ sent_packets: int          # aggregated packets sent
##   ┠╴ sent_bytes: int            # total bytes sent
##   ┠╴ received_packets: int      # aggregated packets received
##   ┖╴ received_bytes: int        # total bytes received
## }
## [/codeblock]
func monitor_snapshot() -> Dictionary:
	return {
		&"drops_unknown_route": _drops_unknown_route,
		&"drops_not_live": _drops_not_live,
		&"drops_no_node": _drops_no_node,
		&"drops_backlog_limit": _drops_backlog_limit,
		&"active_senders": _senders.size(),
		&"sent_packets": _sent_packets,
		&"sent_bytes": _sent_bytes,
		&"received_packets": _received_packets,
		&"received_bytes": _received_bytes,
	}


## Returns the service script type for registration.
func service_type() -> Script:
	return RelayService


func service_entered(mt: MultiplayerTree) -> void:
	register_channel(Command.STATE, _handle_state_payload)
	register_channel(Command.INPUT, _handle_input_payload)

	if not mt.session_entered.is_connected(_on_session_entered):
		mt.session_entered.connect(_on_session_entered)
	if not mt.session_ended.is_connected(_on_session_ended):
		mt.session_ended.connect(_on_session_ended)
	if not mt.peer_disconnected.is_connected(_on_peer_disconnected):
		mt.peer_disconnected.connect(_on_peer_disconnected)

	_ensure_clock_connection()


func service_exiting(mt: MultiplayerTree) -> void:
	if mt.session_entered.is_connected(_on_session_entered):
		mt.session_entered.disconnect(_on_session_entered)
	if mt.session_ended.is_connected(_on_session_ended):
		mt.session_ended.disconnect(_on_session_ended)
	if mt.peer_disconnected.is_connected(_on_peer_disconnected):
		mt.peer_disconnected.disconnect(_on_peer_disconnected)
	_disconnect_clock()


func _on_session_entered() -> void:
	_clock_bind_attempts = 0
	_ensure_clock_connection()


# Drops all per-session state so the tick pump has nothing to touch after the
# session tears down. Deferred to avoid mutating registries mid-teardown,
# mirroring LivenessService.
func _on_session_ended() -> void:
	_clear_session_state.call_deferred()


func _clear_session_state() -> void:
	_senders.clear()
	_unreliable_buffers.clear()
	_reliable_buffers.clear()
	_deferred_calls.clear()
	_txn_book.reject_all("Session ended")


func _on_peer_disconnected(peer_id: int) -> void:
	_txn_book.handle_disconnect(peer_id)


func _handle_state_payload(node: Node, payload: PackedByteArray, sender: int) -> void:
	var entity := NetwEntity.of(node)
	if not entity:
		return
	for sync in entity.synchronizers():
		if sync is PackedSynchronizer and sync.relay_channel() == Command.STATE:
			if sync.get_parent() == node or node.is_ancestor_of(sync):
				sync._receive_relay_payload(payload, sender)


func _handle_input_payload(node: Node, payload: PackedByteArray, sender: int) -> void:
	var entity := NetwEntity.of(node)
	if not entity:
		return
	for sync in entity.synchronizers():
		if sync is PackedSynchronizer and sync.relay_channel() == Command.INPUT:
			if sync.get_parent() == node or node.is_ancestor_of(sync):
				sync._receive_relay_payload(payload, sender)


@rpc("any_peer", "call_remote", "unreliable")
func _carrier_unreliable(bytes: PackedByteArray) -> void:
	_received_packets += 1
	_received_bytes += bytes.size()
	_receive_carrier(bytes, false)


@rpc("any_peer", "call_remote", "reliable")
func _carrier_reliable(bytes: PackedByteArray) -> void:
	_received_packets += 1
	_received_bytes += bytes.size()
	_receive_carrier(bytes, true)


func _receive_carrier(framed_bytes: PackedByteArray, reliable: bool = true) -> void:
	var sender := multiplayer.get_remote_sender_id() if multiplayer else 0
	var frames := _Envelope.unpack_all(framed_bytes)
	for frame in frames:
		_dispatch(frame["route"], frame["comp"], frame["command"], frame["payload"], frame["path"], sender, reliable)


func _dispatch(
		route: int,
		comp: int,
		command: int,
		payload: PackedByteArray,
		path: String,
		sender: int,
		reliable: bool = true,
) -> void:
	var liveness := LivenessService.for_node(self)
	if not liveness:
		return

	if route == 0:
		if command >= 100 and command <= 254:
			var handler: Callable = _handlers.get(command, Callable())
			if handler.is_valid():
				handler.call(null, payload, sender)
		return

	if command == Command.REPLY:
		var r := NetwBitBuffer.Reader.new(payload)
		var txn := NetwCodec.get_safe_varint(r)
		if txn >= 0:
			var body := r.get_aligned_bytes(r.remaining_bytes())
			var value = bytes_to_var(body)
			
			var value_route := 0
			if value is Dictionary:
				if value.has("__escaped_netw_route"):
					value = value.duplicate()
					value["__netw_route"] = value["__escaped_netw_route"]
					value.erase("__escaped_netw_route")
				if value.has("__netw_route"):
					value_route = int(value["__netw_route"])
			
			if value_route > 0 and liveness.route_state(value_route) == LivenessService.State.UNKNOWN:
				liveness.when_live(value_route, func() -> void:
					var resolved_val = liveness.entity_of(value_route).owner if liveness.entity_of(value_route) else null
					_txn_book.handle_reply(txn, sender, resolved_val)
				)
			else:
				var final_val = value
				if value_route > 0:
					var ent = liveness.entity_of(value_route)
					final_val = ent.owner if ent else null
				_txn_book.handle_reply(txn, sender, final_val)
		return

	var state := liveness.route_state(route)
	if state == LivenessService.State.UNKNOWN:
		# A reliable call that beat its target's spawn parks until the route
		# binds. An unreliable call is freshest-wins per-tick traffic, so a lost
		# frame is superseded by the next one, never deferred.
		if command == Command.CALL and reliable:
			defer_call(sender, route, func() -> void:
				_dispatch(route, comp, command, payload, path, sender, reliable)
			)
			return
		_drops_unknown_route += 1
		return
	if state == LivenessService.State.DEAD or state == LivenessService.State.LINGERING:
		_drops_not_live += 1
		return

	var entity := liveness.entity_of(route)
	if not entity or not is_instance_valid(entity.owner):
		_drops_no_node += 1
		return

	var comp_node: Node = entity.owner
	if comp == 255:
		if not _CallRouter.is_safe_component_path(entity.owner, path):
			Netw.dbg.warn("RelayService: path traversal clamp rejected '%s' on '%s'", [path, entity.owner.name])
			return
		comp_node = entity.owner.get_node_or_null(path)
	elif comp > 0:
		if entity._table_poisoned:
			_drops_no_node += 1
			return
		var rel_path := entity._components_by_id.get(comp, NodePath(""))
		if rel_path.is_empty():
			_drops_no_node += 1
			return
		comp_node = entity.owner.get_node_or_null(rel_path)

	if not is_instance_valid(comp_node):
		_drops_no_node += 1
		return

	match command:
		Command.CALL:
			_call_router.handle_call(entity, comp_node, payload, sender)
		Command.STATE:
			_handle_state_payload(comp_node, payload, sender)
		Command.INPUT:
			_handle_input_payload(comp_node, payload, sender)
		Command.VAR_SYNC:
			_var_signal_router.handle_var_sync(entity, comp_node, payload, sender)
		Command.SIGNAL:
			_var_signal_router.handle_signal(entity, comp_node, payload, sender)
		Command.ACTION:
			var handler: Callable = _handlers.get(command, Callable())
			if handler.is_valid():
				handler.call(entity, payload, sender)
		_:
			if command >= 100 and command <= 254:
				var handler: Callable = _handlers.get(command, Callable())
				if handler.is_valid():
					handler.call(entity, payload, sender)


# Clock tick callback that pumps registered senders.
func _on_clock_tick(_delta: float, tick: int) -> void:
	_pump_senders(tick)
	flush_all_buffers()


func _pump_senders(tick: int) -> void:
	var liveness := LivenessService.for_node(self)
	if not liveness:
		return

	# Prune freed entries first by index. Reading a freed instance into a
	# typed loop variable would itself raise "invalid freed instance" and null
	# the variable, so erase(null) would never remove the real entry and the
	# error would repeat every tick. Untyped index reads avoid that.
	for i in range(_senders.size() - 1, -1, -1):
		if not is_instance_valid(_senders[i]):
			_senders.remove_at(i)

	for sender: PackedSynchronizer in _senders.duplicate():
		if not is_instance_valid(sender):
			continue
		if not sender.is_inside_tree():
			continue
		if not sender.is_multiplayer_authority():
			continue

		var entity := NetwEntity.of(sender)
		if not entity:
			continue
		var route := liveness.route_of(entity)
		if route <= 0:
			continue

		var recipients := sender.relay_recipients(liveness, entity)
		if recipients.is_empty():
			continue

		var result: Array = sender._relay_outgoing_bytes(tick)
		var bytes: PackedByteArray = result[0]
		var reliable: bool = result[1]
		if bytes.is_empty():
			continue

		var cmd = Command.STATE if sender.relay_channel() == 0 else Command.INPUT
		for peer_id in recipients:
			send_to(peer_id, route, cmd, bytes, reliable)


func _ensure_clock_connection() -> void:
	if _clock_connected:
		return
	var clock := MultiplayerClock.for_node(self)
	if clock:
		if not clock.after_tick.is_connected(_on_clock_tick):
			clock.after_tick.connect(_on_clock_tick)
		_clock_connected = true
		return
	_clock_bind_attempts += 1
	if _clock_bind_attempts <= _MAX_CLOCK_BIND_ATTEMPTS and is_inside_tree() \
			and not get_tree().process_frame.is_connected(_ensure_clock_connection):
		get_tree().process_frame.connect(
			_ensure_clock_connection,
			CONNECT_ONE_SHOT,
		)


func _disconnect_clock() -> void:
	if _clock_connected:
		var clock := MultiplayerClock.for_node(self)
		if clock and clock.after_tick.is_connected(_on_clock_tick):
			clock.after_tick.disconnect(_on_clock_tick)
		_clock_connected = false


func _tree() -> MultiplayerTree:
	return MultiplayerTree.resolve(self)


# Inner Classes

class _Envelope:
	static func pack(route: int, comp: int, command: int, payload: PackedByteArray, path: String = "") -> PackedByteArray:
		var w := NetwBitBuffer.Writer.new()
		NetwCodec.put_varint(w, route)
		w.put_aligned_u8(comp)
		w.put_aligned_u8(command)
		
		var final_payload := payload
		if comp == 255:
			var path_bytes := path.to_utf8_buffer()
			var pw := NetwBitBuffer.Writer.new()
			NetwCodec.put_varint(pw, path_bytes.size())
			pw.put_aligned_bytes(path_bytes)
			pw.put_aligned_bytes(payload)
			final_payload = pw.to_bytes()
		
		NetwCodec.put_varint(w, final_payload.size())
		w.put_aligned_bytes(final_payload)
		return w.to_bytes()

	static func unpack_next(r: NetwBitBuffer.Reader) -> Dictionary:
		var route := NetwCodec.get_safe_varint(r)
		if route < 0:
			return {}
		var comp := r.get_aligned_u8()
		var command := r.get_aligned_u8()
		var payload_len := NetwCodec.get_safe_varint(r)
		if payload_len < 0:
			return {}
		var payload_bytes := r.get_aligned_bytes(payload_len)
		
		var path := ""
		var actual_payload := payload_bytes
		if comp == 255:
			var pr := NetwBitBuffer.Reader.new(payload_bytes)
			var path_len := NetwCodec.get_safe_varint(pr)
			if path_len >= 0:
				var path_bytes := pr.get_aligned_bytes(path_len)
				path = path_bytes.get_string_from_utf8()
				actual_payload = pr.get_aligned_bytes(pr.remaining_bytes())
		
		return {
			"route": route,
			"comp": comp,
			"command": command,
			"payload": actual_payload,
			"path": path,
		}

	static func unpack_all(framed_bytes: PackedByteArray) -> Array[Dictionary]:
		var out: Array[Dictionary] = []
		if framed_bytes.is_empty():
			return out
		var r := NetwBitBuffer.Reader.new(framed_bytes)
		while r.remaining_bytes() > 0:
			var frame := unpack_next(r)
			if frame.is_empty():
				break
			out.append(frame)
		return out


class _CallRouter:
	extends RefCounted
	
	var _relay: RelayService
	
	func _init(relay: RelayService) -> void:
		_relay = relay

	static func is_safe_component_path(entity_root: Node, relative_path: String) -> bool:
		if relative_path.begins_with("/") or relative_path.contains("..") or relative_path.begins_with("res://") or relative_path.begins_with("user://"):
			return false
		var target := entity_root.get_node_or_null(relative_path)
		if not target:
			return false
		return target == entity_root or entity_root.is_ancestor_of(target)

	func handle_call(entity: NetwEntity, comp_node: Node, payload: PackedByteArray, sender: int) -> void:
		var r := NetwBitBuffer.Reader.new(payload)
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

		var method_raw: Variant = RelayService._read_method_token(r)
		var method: StringName = &""
		if method_raw is int:
			method = RelayService.get_method_name_by_id(script, method_raw)
		else:
			method = StringName(method_raw)

		if method.is_empty():
			return

		# Argument decoding needs the method's configured quantizers and its
		# declared parameter types, so it happens after the method resolves.
		var encoded_args: Array = RelayService._read_call_args(
			r,
			RelayService._method_quantizers_for(script, method),
			RelayService.get_method_arg_types(script, method),
		)

		var opt := RelayService.get_rpc_options(script, method)
		if not opt and not script.get_rpc_config().has(method):
			Netw.dbg.warn("RelayService: method '%s' not registered or annotated on '%s'", [method, comp_node.name])
			return

		if not _is_sender_allowed(opt, script, method, entity, comp_node, sender):
			Netw.dbg.warn("RelayService: unauthorized RPC sender %d for '%s' on '%s'", [sender, method, comp_node.name])
			return

		if not RelayService.validate_argument_count(script, method, encoded_args.size()):
			Netw.dbg.warn("RelayService: arity mismatch for '%s' on '%s'", [method, comp_node.name])
			return

		var liveness: LivenessService = _relay.liveness
		var args: Array = []
		for encoded in encoded_args:
			var decoded_arg = encoded
			if decoded_arg is Dictionary:
				if decoded_arg.has("__escaped_netw_route"):
					decoded_arg = decoded_arg.duplicate()
					decoded_arg["__netw_route"] = decoded_arg["__escaped_netw_route"]
					decoded_arg.erase("__escaped_netw_route")

			var arg_route := _call_arg_route(decoded_arg)
			if arg_route > 0:
				var arg_entity: NetwEntity = liveness.entity_of(arg_route)
				if arg_entity == null and liveness.route_state(arg_route) == LivenessService.State.UNKNOWN:
					liveness.when_live(arg_route, func() -> void:
						handle_call(entity, comp_node, payload, sender)
					)
					return

				if _relay.multiplayer and _relay.multiplayer.is_server() and arg_entity:
					if not liveness.is_live_for(sender, arg_entity):
						Netw.dbg.warn("RelayService: sender %d interest view does not admit route %d", [sender, arg_route])

				args.append(arg_entity.owner if arg_entity else null)
			else:
				args.append(decoded_arg)

		var is_server_peer := _relay.multiplayer and _relay.multiplayer.is_server()
		
		# Relayed Request (Two-Way Client-to-Client Request)
		if txn > 0 and target_peer != 1 and is_server_peer and sender != 1:
			var promise := _relay.request_call(
				target_peer, Callable(comp_node, method), args
			)
			if promise:
				promise.then(func(val: Variant) -> void:
					_relay.send_reply(sender, entity.route, txn, val)
				)
				promise.catch_error(func(err: String) -> void:
					_relay.send_reply(sender, entity.route, txn, null)
				)
			return

		# Relayed Broadcast / Direct Call (One-Way Client-to-Client/All)
		var target := _relay._resolve_comp(entity, comp_node)
		var comp := target["comp"] as int
		var path := target["path"] as String
		var reliable := (
			RelayService.get_method_reliable(script, method)
			if script else true
		)

		if txn == 0 and target_peer != 1 and is_server_peer and sender != 1:
			if target_peer == 0:
				var recipients := liveness.live_peers(entity)
				recipients.erase(sender)
				for recipient in recipients:
					_relay.send_to(
						recipient, entity.route, RelayService.Command.CALL,
						payload, reliable, comp, path
					)
			else:
				if liveness.is_live_for(target_peer, entity):
					_relay.send_to(
						target_peer, entity.route, RelayService.Command.CALL,
						payload, reliable, comp, path
					)
			# Do not execute locally on the Server if it's client-to-client only
			if target_peer != 0:
				return

		# Execute locally
		if opt and not opt.defer_signal_name.is_empty():
			if not Netw._defer_signal_fired(comp_node, opt.defer_signal_name):
				if comp_node.has_signal(opt.defer_signal_name):
					comp_node.connect(opt.defer_signal_name, func() -> void:
						_execute_call(entity, comp_node, method, args, txn, sender)
					, CONNECT_ONE_SHOT)
					return

		_execute_call(entity, comp_node, method, args, txn, sender)


	func _execute_call(entity: NetwEntity, comp_node: Node, method: StringName, args: Array, txn: int, sender: int) -> void:
		var result = comp_node.callv(method, args)
		if txn > 0:
			if result is NetwPromise:
				result.then(func(val: Variant) -> void:
					_relay.send_reply(sender, entity.route, txn, val)
				)
				result.catch_error(func(err: String) -> void:
					_relay.send_reply(sender, entity.route, txn, null)
				)
			else:
				_relay.send_reply(sender, entity.route, txn, result)


	func _is_sender_allowed(opt: Netw.RpcOptions, script: Script, method: StringName, entity: NetwEntity, node: Node, sender: int) -> bool:
		if sender == 1:
			return true
		if opt and opt.is_controller_only:
			return entity != null and sender == entity.controller
		var rpc_mode = RelayService.get_method_rpc_mode(script, method)
		if rpc_mode == 2:
			return sender == node.get_multiplayer_authority()
		elif rpc_mode == 1:
			return true
		return false


	func _call_arg_route(encoded: Variant) -> int:
		if encoded is Dictionary and (encoded as Dictionary).size() == 1 \
				and (encoded as Dictionary).has("__netw_route"):
			return int((encoded as Dictionary)["__netw_route"])
		return 0


class _TxnBook:
	extends RefCounted
	
	var _relay: RelayService
	var _active: Dictionary = {}
	var _next_txn_id: int = 1

	func _init(relay: RelayService) -> void:
		_relay = relay

	func make_id() -> int:
		var id := _next_txn_id
		_next_txn_id += 1
		return id

	func register(txn_id: int, promise: RefCounted, target: Variant, deadline: int) -> void:
		_active[txn_id] = {
			"promise": promise,
			"target": target,
			"deadline": deadline
		}

	func handle_reply(txn_id: int, sender: int, value: Variant) -> void:
		if not _active.has(txn_id):
			return
		var info: Dictionary = _active[txn_id]
		var promise: RefCounted = info["promise"]
		var target: Variant = info["target"]

		if promise is NetwPromise:
			if target != sender:
				return
			promise.resolve(value)
			_active.erase(txn_id)
		elif promise is NetwGroupPromise:
			var expected: Array = target
			if not expected.has(sender):
				return
			promise.resolve_peer(sender, value)
			if promise.is_completed:
				_active.erase(txn_id)

	func sweep(current: int) -> void:
		var expired: Array = []
		for id in _active:
			var info: Dictionary = _active[id]
			if current >= info["deadline"]:
				expired.append(id)
		for id in expired:
			var info: Dictionary = _active[id]
			var promise: RefCounted = info["promise"]
			promise.reject("Timeout")
			_active.erase(id)

	func reject_all(reason: String) -> void:
		var pending := _active
		_active = {}
		for id in pending:
			var info: Dictionary = pending[id]
			var promise: RefCounted = info["promise"]
			promise.reject(reason)


	func handle_disconnect(peer_id: int) -> void:
		var finished: Array = []
		for id in _active:
			var info: Dictionary = _active[id]
			var promise: RefCounted = info["promise"]
			var target: Variant = info["target"]
			if promise is NetwPromise:
				if target == peer_id:
					promise.reject("Peer disconnected")
					finished.append(id)
			elif promise is NetwGroupPromise:
				var expected: Array = target
				if expected.has(peer_id):
					promise.remove_peer(peer_id)
					if promise.is_completed:
						finished.append(id)
		for id in finished:
			_active.erase(id)


class _VarSignalRouter:
	extends RefCounted
	
	var _relay: RelayService

	func _init(relay: RelayService) -> void:
		_relay = relay

	func handle_var_sync(entity: NetwEntity, comp_node: Node, payload: PackedByteArray, sender: int) -> void:
		var decoded: Variant = bytes_to_var(payload)
		if not (decoded is Array) or (decoded as Array).size() != 2:
			return
		var prop: StringName = StringName(decoded[0])
		var val: Variant = decoded[1]

		if not _is_write_allowed(comp_node, prop, sender):
			Netw.dbg.warn("RelayService: unauthorized var sync for '%s' from peer %d", [prop, sender])
			return

		comp_node.set(prop, val)

		var mt := MultiplayerTree.resolve(comp_node)
		if mt and mt.is_host:
			var liveness := mt.get_service(LivenessService) as LivenessService
			var route := liveness.route_of(entity)
			var recipients := liveness.live_peers(entity)
			recipients.erase(sender)
			for recipient in recipients:
				_relay.send_to(recipient, route, RelayService.Command.VAR_SYNC, payload, true)

	func handle_signal(entity: NetwEntity, comp_node: Node, payload: PackedByteArray, sender: int) -> void:
		var decoded: Variant = bytes_to_var(payload)
		if not (decoded is Array) or (decoded as Array).size() != 2:
			return
		var signal_name: StringName = StringName(decoded[0])
		var args: Array = decoded[1]

		if not _is_signal_allowed(comp_node, signal_name, sender):
			Netw.dbg.warn("RelayService: unauthorized signal emission '%s' from peer %d", [signal_name, sender])
			return

		var callable := Callable(comp_node, &"emit_signal")
		callable.callv([signal_name] + args)

		var mt := MultiplayerTree.resolve(comp_node)
		if mt and mt.is_host:
			var liveness := mt.get_service(LivenessService) as LivenessService
			var route := liveness.route_of(entity)
			var recipients := liveness.live_peers(entity)
			recipients.erase(sender)
			for recipient in recipients:
				_relay.send_to(recipient, route, RelayService.Command.SIGNAL, payload, true)

	func _is_write_allowed(node: Node, property: StringName, sender: int) -> bool:
		if sender == 1:
			return true
		var script := node.get_script() as Script
		if not script:
			return false
		var configs: Dictionary = Netw._var_configs.get(script, {})
		if not configs.has(property):
			return false
		var opt: Netw.VarOptions = configs[property]
		match opt.write_policy:
			Netw.Policy.AUTHORITY:
				return sender == node.get_multiplayer_authority()
			Netw.Policy.CONTROLLER:
				var entity := NetwEntity.of(node)
				return entity != null and sender == entity.controller
			Netw.Policy.ANY_PEER:
				return true
		return false

	func _is_signal_allowed(node: Node, signal_name: StringName, sender: int) -> bool:
		if sender == 1:
			return true
		var script := node.get_script() as Script
		if not script:
			return false
		# Unregistered signals are denied, mirroring var sync. Without this a
		# node's authority (an input subtree can be client-owned) could inject
		# arbitrary signal emissions, including framework signals.
		var configs: Dictionary = Netw._signal_configs.get(script, {})
		if not configs.has(signal_name):
			return false
		var emit_policy = configs[signal_name].emit_policy
		match emit_policy:
			Netw.Policy.AUTHORITY:
				return sender == node.get_multiplayer_authority()
			Netw.Policy.CONTROLLER:
				var entity := NetwEntity.of(node)
				return entity != null and sender == entity.controller
			Netw.Policy.ANY_PEER:
				return true
		return false
