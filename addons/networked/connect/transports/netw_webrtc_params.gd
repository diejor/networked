## The typed inputs a [WebRTCTransport] signals and hosts with.
##
## WebRTC needs no port because peers meet through a signaler rather than a
## bound socket, so the one authored input is the namespace that isolates room
## codes. [WebRTCLoopbackTransport] recognizes these too and ignores the
## namespace, since an in-process handshake never reaches a tracker.
## [codeblock]
## var webrtc := NetwWebRTCParams.new()
## webrtc.signaling_namespace = "bomber-v2"
## config.transport = webrtc        # config.scheme now reads &"webrtc"
## [/codeblock]
class_name NetwWebRTCParams
extends NetwTransportParams

## Namespace isolating signaling and room codes on a shared public tracker.
##
## Two builds sharing a tracker with different namespaces cannot see each
## other's rooms. Leave it empty to use the transport's own
## [member WebRTCTransport.signaling_namespace].
@export var signaling_namespace: String = ""


func _scheme() -> StringName:
	return &"webrtc"


func _to_dict() -> Dictionary:
	return { "signaling_namespace": signaling_namespace }


func _from_dict(source: Dictionary) -> PackedStringArray:
	signaling_namespace = String(
		source.get("signaling_namespace", signaling_namespace),
	)
	return PackedStringArray(["signaling_namespace"])
