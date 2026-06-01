## In-process [WebTorrentTrackerClient] double that records announces.
##
## It never opens a socket. [method connect_to] reports an immediately open
## route so a [TrackerSignaler] under test announces, and every
## [method broadcast] and [method send] is captured in [member announces] for
## shape assertions. No tracker traffic leaves the process.
## [codeblock]
## var fake := FakeTrackerClient.new()
## signaler._tracker = fake          # or inject via a _make_tracker override
## ...drive the signaler...
## assert_array(fake.announces).has_size(1)
## [/codeblock]
class_name FakeTrackerClient
extends WebTorrentTrackerClient

## Every payload the signaler broadcast or sent, in order.
var announces: Array[Dictionary] = []


func connect_to(_urls: Array[String]) -> Error:
	connected.emit()
	return OK


func poll() -> void:
	pass


func broadcast(data: Dictionary) -> void:
	announces.append(data)


func send(_ws: WebSocketPeer, data: Dictionary) -> void:
	announces.append(data)


func has_open() -> bool:
	return true


func is_active() -> bool:
	return true


func close() -> void:
	pass


## Returns the recorded announces that carry a real offer or answer, dropping
## the trickle and presence announces so a test can assert ICE bundling.
func sdp_announces(slot: String) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	for data in announces:
		if slot == "offer":
			var offers: Array = data.get("offers", [])
			for entry: Variant in offers:
				if typeof(entry) == TYPE_DICTIONARY \
						and (entry as Dictionary).get("offer", {}).get("type") == "offer":
					out.append(data)
					break
		elif data.has("answer") \
				and (data["answer"] as Dictionary).get("type") == "answer":
			out.append(data)
	return out
