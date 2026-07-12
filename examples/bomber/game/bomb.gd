extends Area2D

var in_area: Array = []
var from_player: int = 0


func _notification(what: int) -> void:
	if what != NOTIFICATION_PARENTED:
		return
	var entity := NetwEntity.resolve(self)
	if not entity:
		return
	Netw.configure_property(self, &"position").on_spawn()
	Netw.configure_property(self, &"from_player").on_spawn()


# Called from the animation.
func explode() -> void:
	if not is_multiplayer_authority():
		# Explode only on authority.
		return

	for p: Object in in_area:
		if p.has_method(&"exploded"):
			if p.get(&"_exploding") == true:
				continue
			# Checks if there is wall in between bomb and the object.
			var world_state := get_world_2d().direct_space_state
			var query := PhysicsRayQueryParameters2D.create(
				position,
				p.position,
			)
			query.hit_from_inside = true
			# intersect_ray returns an empty Dictionary on a miss.
			var result := world_state.intersect_ray(query)
			if result.get(&"collider") is not TileMap:
				# Exploded can only be called by the authority,
				# but will also be called locally.
				p.exploded.rpc(from_player)


func done() -> void:
	if is_multiplayer_authority():
		queue_free()


func _on_bomb_body_enter(body: Node2D) -> void:
	if not body in in_area:
		in_area.append(body)


func _on_bomb_body_exit(body: Node2D) -> void:
	in_area.erase(body)
