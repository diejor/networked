## A raw byte channel multiplexed over the session carrier, addressed by a
## channel [member id].
##
## Use this for session-scoped traffic that has no entity to associate with,
## such as a voice stream, a chat line, or a compressed world grid. Ids run
## [code]100[/code] to [code]254[/code]. Give each channel a distinct
## [member id] and call [method register] once per peer.
## [codeblock]
## # both peers, once:
## var chat := Netw.channel(self, 120)
## chat.register(func(sender: int, payload: PackedByteArray) -> void:
##     show_line(sender, payload.get_string_from_utf8())
## )
##
## # any peer, any time:
## chat.broadcast("gg".to_utf8_buffer())
## [/codeblock]
## Pass [code]batched[/code] as [code]true[/code] to [method send] or
## [method broadcast] to aggregate payloads into the shared peer buffers
## flushed by [method ReplicationCore.flush_all_buffers].
class_name NetwChannel
extends RefCounted

## The channel identifier. Must be between 100 and 254.
var id: int

var _replication: ReplicationCore


func _init(p_id: int, p_replication: ReplicationCore) -> void:
	assert(
		p_id >= 100 and p_id <= 254,
		"NetwChannel: ID must be between 100 and 254.",
	)
	id = p_id
	_replication = p_replication


## Sends the custom channel payload to the specified [param peer_id].
##
## With [param batched] as [code]true[/code], the payload queues into the
## peer aggregation buffers flushed by [method ReplicationCore.flush_all_buffers].
func send(peer_id: int, payload: PackedByteArray, reliable: bool = true, batched: bool = false) -> void:
	_replication.send_to(peer_id, 0, id, payload, reliable, 0, "", batched)


## Broadcasts the custom channel payload to all other connected peers.
func broadcast(payload: PackedByteArray, reliable: bool = true, batched: bool = false) -> void:
	_replication.send_to(0, 0, id, payload, reliable, 0, "", batched)


## Registers a [param handler] to receive payloads for this custom channel.
##
## The [param handler] is called as:
## [code]handler(sender: int, payload: PackedByteArray)[/code].
func register(handler: Callable) -> void:
	_replication.register_channel(
		id,
		func(_entity, payload: PackedByteArray, sender: int) -> void:
			handler.call(sender, payload)
	)
