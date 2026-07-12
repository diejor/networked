## Test fixture player whose [code]position[/code] is a persistence column.
##
## The archetype declares the table only. Suites inject the database per instance
## through the [constant NetwPersistenceInterface.PersistenceEngine.META_DATABASE]
## meta so each test can point at its own temp-dir backend.
extends Node2D


func _init() -> void:
	var entity := NetwEntity.resolve(self)
	entity.initial_controller = NetwEntity.InitialController.REPRESENTED_PEER

	Netw.configure_persistence(self).table(&"players_save")
	Netw.configure_property(self, &"position").persisted()
