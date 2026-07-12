## Minimal test fixture player with no persisted state.
extends Node2D


func _init() -> void:
	var entity := NetwEntity.resolve(self)
	entity.initial_controller = NetwEntity.InitialController.REPRESENTED_PEER
