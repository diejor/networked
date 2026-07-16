## The default [WebRTCTransport], signaling over WebTorrent trackers.
##
## Needs no signaling server: peers rendezvous on a tracker swarm keyed by the
## room id, and [WebRTCPeerView] drives the [WebRTCSession] over the
## [TrackerSignaler] this transport supplies.
class_name TrackerWebRTCTransport
extends WebRTCTransport

## WebTorrent-compatible tracker URLs used for signaling.
var trackers: Array[String] = [
	"wss://tracker.openwebtorrent.com",
	"wss://tracker.webtorrent.dev",
	"wss://tracker.btorrent.xyz",
]


func _make_signaler() -> WebRTCSignaler:
	return TrackerSignaler.new(
		trackers,
		signaling_namespace,
		room_code_characters,
	)
