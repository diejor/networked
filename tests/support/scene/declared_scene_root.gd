## Scene root that declares itself and carries its own replicated state.
##
## This is the conformance shape for "a scene is an ordinary entity": the
## declaration and the property mark are two independent lines in the same
## [method Object._init], neither aware of the other. If the property reaches
## admitted peers, a scene replicates like anything else and needs no bespoke
## per-scene API for match state.
class_name DeclaredSceneRoot
extends Node2D

## Session tick a countdown expires on, or [code]0[/code] when idle. Replicated
## on the spawn packet, so a late joiner reads the same target as everyone else.
var countdown_target := 0

## Bumped by the server so a post-spawn change has something observable to
## carry.
var round_number := 0


func _init() -> void:
	Netw.configure_multiplayer_scene(self).labeled(&"DeclaredArena")
	Netw.configure_property(self, &"countdown_target").on_spawn()
	Netw.configure_property(self, &"round_number").state()
