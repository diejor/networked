## Autoload that installs the root-override session at startup.
##
## The [code]networked/install_as_default[/code] project setting turns this on.
## When it is set, this runs before the main scene and calls
## [method NetwMultiplayer.install_as_default], so [code]multiplayer[/code] in
## every script is the session and a native scene change keeps it alive. When the
## setting is off this does nothing, which is the default for projects that scope
## sessions to a [MultiplayerTree] instead.
extends Node

const _SETTING := "networked/install_as_default"


func _enter_tree() -> void:
	if ProjectSettings.get_setting(_SETTING, false):
		NetwMultiplayer.install_as_default(get_tree())
