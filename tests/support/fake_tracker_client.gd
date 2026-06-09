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


## Returns the recorded directed announces whose answer slot carries the given
## inner type ([code]"offer"[/code] or [code]"answer"[/code]), dropping presence
## and stop announces so a test can assert the directed bundle shape.
func sdp_announces(type: String) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	for data in announces:
		var answer: Variant = data.get("answer")
		if typeof(answer) == TYPE_DICTIONARY \
				and (answer as Dictionary).get("type") == type:
			out.append(data)
	return out
