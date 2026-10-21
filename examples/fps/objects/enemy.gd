extends Node3D

var player: Node3D

@onready var raycast = $RayCast
@onready var muzzle_a = $MuzzleA
@onready var muzzle_b = $MuzzleB
@onready var scene := Netw.scene(self)

var health := 100
var time := 0.0
var target_position: Vector3
var destroyed := false

var attacks := 0:
	set(value):
		if value == attacks:
			return
		attacks = value

		# Play muzzle flash animation(s)

		muzzle_a.frame = 0
		muzzle_a.play("default")
		muzzle_a.rotation_degrees.z = randf_range(-45, 45)

		muzzle_b.frame = 0
		muzzle_b.play("default")
		muzzle_b.rotation_degrees.z = randf_range(-45, 45)

		Audio.play("sounds/enemy_attack.ogg")


func _init():
	Netw.configure_entity(self).display.visual_root = ^"enemy-flying"
	Netw.configure_property(self, &"position").state() \
			.interpolate(NetwInterpolate.new().lerp())
	Netw.configure_property(self, &"quaternion").state() \
			.interpolate(NetwInterpolate.new().slerp())
	Netw.configure_property(self, &"attacks").state()
	Netw.configure_rpc(damage).call_local()


func _notification(what):
	if what == NOTIFICATION_PARENTED:
		NetwEntity.of(self).name = StringName(name)

# When ready, save the initial position


func _ready():
	target_position = position


func _process(delta):
	if not is_multiplayer_authority():
		return

	player = null
	for body: NetwEntity in scene.player_entities:
		if player == null or position.distance_to(body.root.position) < position.distance_to(player.position):
			player = body.root

	if player == null:
		return

	self.look_at(player.position + Vector3(0, 0.5, 0), Vector3.UP, true) # Look at player
	target_position.y += (cos(time * 5) * 1) * delta # Sine movement (up and down)

	time += delta

	position = target_position

# Take damage from player


func damage(amount):
	Audio.play("sounds/enemy_hurt.ogg")

	health -= amount

	if health <= 0 and !destroyed:
		destroy()

# Destroy the enemy when out of health


func destroy():
	Audio.play("sounds/enemy_destroy.ogg")

	destroyed = true
	if is_multiplayer_authority():
		Netw.despawn(self)

# Shoot when timer hits 0


func _on_timer_timeout():
	if not is_multiplayer_authority():
		return

	raycast.force_raycast_update()

	if raycast.is_colliding():
		var collider = raycast.get_collider()

		if collider.has_method("damage"): # Raycast collides with player
			attacks += 1

			Netw.rpc(collider.damage, 5) # Apply damage to player
