## Serializable container for a single debug event forwarded via the relay bridge.
##
## Used by [NetDebugRelay] to carry a [code]msg[/code] + [code]data[/code] pair
## across the game network without the relay needing to understand the payload.
class_name NetRelayPayload
extends RefCounted

var msg: String = ""
var data: Dictionary = {}
var source_tree_name: String = ""


func to_bytes() -> PackedByteArray:
	return var_to_bytes({
		msg = msg,
		data = data,
		source_tree_name = source_tree_name,
	})


static func from_bytes(bytes: PackedByteArray) -> NetRelayPayload:
	var d: Dictionary = bytes_to_var(bytes)
	var p := NetRelayPayload.new()
	p.msg = d.get("msg", "")
	p.data = d.get("data", {})
	p.source_tree_name = d.get("source_tree_name", "")
	return p
