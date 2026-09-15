## ICE server addresses and credentials for [ConnectWebRTCConfig].
class_name ConnectIceServerConfig
extends Resource

@export var urls: PackedStringArray = ["stun:stun.l.google.com:19302"]
@export var username: String = ""
@export var credential: String = ""


func settings() -> Dictionary:
	var result := { urls = Array(urls) }
	if not username.is_empty():
		result.username = username
	if not credential.is_empty():
		result.credential = credential
	return result
