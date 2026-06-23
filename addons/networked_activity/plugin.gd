## Editor plugin for the Networked Activity addon.
##
## The addon's runtime is plain [code]class_name[/code] script, so it works
## whether or not this plugin is enabled. The plugin exists only to register the
## addon with the editor and host editor-side settings. The
## [DiscordActivityService] auto-registers itself on a [MultiplayerTree] the same
## way every other [NetwService] does, so nothing is added as an autoload here.
@tool
extends EditorPlugin


func _enter_tree() -> void:
	pass


func _exit_tree() -> void:
	pass
