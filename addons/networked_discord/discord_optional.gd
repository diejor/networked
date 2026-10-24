## Adds a scene as its child only when the Discord Social SDK is installed.
##
## It names no Discord class and holds [member scene_file] as a path, so a
## scene holding it opens and runs where Discord is missing. That covers
## Linux older than glibc 2.38, macOS, and an export that leaves Discord out.
## [codeblock]
## var discord := DiscordOptional.new()
## discord.scene_file = "res://game/discord.tscn"
## add_child(discord)
## [/codeblock]
## A script that names [DiscordSocial] does not parse without Discord, so keep
## it inside [member scene_file]. That scene usually holds a [DiscordSocial]
## and [code]res://addons/networked_discord/ui/discord_panel.tscn[/code].
class_name DiscordOptional
extends Node

## The path of the [PackedScene] added as a child in [method Node._ready]
## when Discord is installed.
@export_file("*.tscn", "*.scn") var scene_file := ""


func _ready() -> void:
	if not ClassDB.class_exists(&"DiscordClient"):
		return
	var scene: PackedScene = load(scene_file)
	add_child(scene.instantiate())
