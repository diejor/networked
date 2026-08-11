## Autoload that installs the root-override session at startup.
##
## The [code]networked/install_as_default[/code] project setting turns this on.
## When it is set, this runs before the main scene and calls
## [method NetwMultiplayer.install_as_default], so [code]multiplayer[/code] in
## every script is the session and a native scene change keeps it alive.
extends Node

const _SETTING := "networked/install_as_default"

var _api: NetwMultiplayer


func _enter_tree() -> void:
	if ProjectSettings.get_setting(_SETTING, false):
		_api = NetwMultiplayer.install_as_default(get_tree())
		get_tree().process_frame.connect(_api.embedding.settle, CONNECT_ONE_SHOT)


func _exit_tree() -> void:
	if _api == null:
		return
	if get_tree().get_multiplayer() == _api:
		NetwMultiplayer.uninstall_default(get_tree())
	else:
		if _api.has_multiplayer_peer():
			_api.multiplayer_peer.close()
			_api.multiplayer_peer = null
		_api.embedding.dispose()
	_api = null
