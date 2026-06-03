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
]

## Seconds the offer and answer wait for ICE to gather before announcing, so
## candidates bundle into the one offer and one answer the tracker forwards.
## Forwarded to [member TrackerSignaler.gather_grace].
@export_range(0.0, 5.0, 0.05, "suffix:s") var gather_grace: float = 0.4

## Trickles every candidate in its own offer slot instead of bundling, for
## trackers that tolerate the extra announces. Forwarded to
## [member TrackerSignaler.allow_tracker_trickle_ice].
@export var allow_tracker_trickle_ice: bool = false


func _make_signaler() -> WebRTCSignaler:
	var signaler := TrackerSignaler.new(trackers)
	signaler.gather_grace = gather_grace
	signaler.allow_tracker_trickle_ice = allow_tracker_trickle_ice
	return signaler


## Preserves the tracker list and ICE timing after [method Resource.duplicate].
func copy_from(source: BackendPeer) -> void:
	super.copy_from(source)
	if source is TrackerWebRTCBackend:
		var other := source as TrackerWebRTCBackend
		trackers = other.trackers.duplicate()
		gather_grace = other.gather_grace
		allow_tracker_trickle_ice = other.allow_tracker_trickle_ice
