## [WebRTCBackend] signaling over WebTorrent trackers via [TrackerSignaler].
##
## This is the default WebRTC transport. It needs no signaling server. Peers
## rendezvous on a tracker swarm keyed by the room id, and the base class drives
## the [WebRTCSession] over the [TrackerSignaler] this backend supplies.
## [codeblock]
## tree.backend = TrackerWebRTCBackend.new()
## await tree.host_player(payload)
##
## target.backend = TrackerWebRTCBackend.new()
## target.address = room_hash
## await tree.join(target, payload)
## [/codeblock]
@tool
class_name TrackerWebRTCBackend
extends WebRTCBackend

## WebTorrent compatible tracker URLs used for signaling.
@export var trackers: Array[String] = [
	"wss://tracker.openwebtorrent.com",
	"wss://tracker.webtorrent.dev",
	"wss://tracker.btorrent.xyz",
]


func make_signaler() -> WebRTCSignaler:
	return TrackerSignaler.new(
		trackers,
		signaling_namespace,
		room_code_characters,
	)


## Preserves the tracker list after [method Resource.duplicate].
func copy_from(source: BackendPeer) -> void:
	super.copy_from(source)
	if source is TrackerWebRTCBackend:
		var other := source as TrackerWebRTCBackend
		trackers = other.trackers.duplicate()
