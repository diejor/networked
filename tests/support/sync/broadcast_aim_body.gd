## Minimal broadcast-set entity root for the masked-broadcast suite.
##
## Declares a masked [code]aim_dir[/code] broadcast set through
## [method Netw.configure_property], a controller-authored display stream that
## fans out to every observer and records into no timeline. Only the entity's
## [member NetwEntity.controller] authors it, so a non-controlling observer stays
## silent toward the author.
class_name BroadcastAimBody
extends Node2D

var aim_dir: Vector2 = Vector2.ZERO


func _init() -> void:
	Netw.configure_property(self, &"aim_dir").broadcast().masked()
