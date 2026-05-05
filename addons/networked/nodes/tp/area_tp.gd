@tool
class_name AreaTP2D
extends Area2D

## A 2D trigger area that teleports entities possessing a [TPComponent] to a designated [SceneNodePath].
##
## Teleportation logic is executed strictly on the client that owns the entering body.
## The server and non-authoritative clients will ignore the trigger.
## For 3D physics bodies use [AreaTP3D] instead.

## Emitted locally immediately before the entity is teleported.
## [param body] is guaranteed to have a valid [TPComponent] when this is emitted.
signal teleport(body: Node2D)

## The destination path where the entity will be teleported. 
## Enforced via the inspector to point to a [Marker2D].
@export_custom(PROPERTY_HINT_RESOURCE_TYPE, "SceneNodePath:Marker2D")
var target_tp: SceneNodePath:
	set(value):
		if target_tp == value:
			return
			
		if target_tp and target_tp.changed.is_connected(update_configuration_warnings):
			target_tp.changed.disconnect(update_configuration_warnings)
			
		target_tp = value
		
		if target_tp and not target_tp.changed.is_connected(update_configuration_warnings):
			target_tp.changed.connect(update_configuration_warnings)
		
		update_configuration_warnings()


func _init() -> void:
	unique_name_in_owner = true
	body_entered.connect(_on_body_entered)


func _get_configuration_warnings() -> PackedStringArray:
	var warnings := PackedStringArray()
	
	if not target_tp:
		warnings.append("A target_tp SceneNodePath must be assigned.")
	elif "_editor_property_warnings" in target_tp and not target_tp._editor_property_warnings.is_empty():
		warnings.append("Target TP Error: " + target_tp._editor_property_warnings)
	elif not target_tp.is_valid():
		warnings.append("The assigned target_tp is missing a scene or node path.")
		
	return warnings


func _on_body_entered(body: Node2D) -> void:
	var tp: TPComponent = body.get_node_or_null("%TPComponent")
	if tp == null or not tp.is_multiplayer_authority():
		return
	
	assert(target_tp and target_tp.is_valid(), "AreaTP2D: `target_tp` is not valid.")
	
	teleport.emit(body)
	tp.teleport.call_deferred(target_tp)
