extends CharacterBody3D

@export_subgroup("Properties")
@export var movement_speed = 5
@export_range(0, 100) var number_of_jumps: int = 2
@export var jump_strength = 8

@export_subgroup("Weapons")
@export var weapons: Array[Weapon] = []

var weapon: Weapon
var weapon_index := 0:
	set(value):
		if value == weapon_index:
			return
		weapon_index = value
		for n in hand.get_children():
			n.queue_free()
		hand.add_child(weapons[weapon_index].model.instantiate())

var shots := 0:
	set(value):
		if value == shots:
			return
		shots = value
		Audio.play(weapons[weapon_index].sound_shoot)

		if not entity.is_controlled_locally:
			return

		# Set muzzle flash position, play animation

		muzzle.play("default")

		muzzle.rotation_degrees.z = randf_range(-45, 45)
		muzzle.scale = Vector3.ONE * randf_range(0.40, 0.75)
		muzzle.position = container.position - weapon.muzzle_position

var mouse_sensitivity = 700
var gamepad_sensitivity := 0.075

var mouse_captured := true

var movement_velocity: Vector3
var rotation_target: Vector3

var input_mouse: Vector2

var health: int = 100:
	set(value):
		health = value
		health_updated.emit(health) # Update health on HUD
var gravity := 0.0

var previously_floored := false

var jumps_remaining: int

var container_offset = Vector3(1.2, -1.1, -2.75)

var tween: Tween

signal health_updated

@onready var camera = $Head/Camera
@onready var raycast = $Head/Camera/RayCast
@onready var muzzle = $Head/Camera/SubViewportContainer/SubViewport/CameraItem/Muzzle
@onready var container = $Head/Camera/SubViewportContainer/SubViewport/CameraItem/Container
@onready var sound_footsteps = $SoundFootsteps
@onready var blaster_cooldown = $Cooldown
@onready var spawn_position := position
@onready var hand = $Visual/Character/Root/Skeleton3D/RightHand/Weapon
@onready var animation = $Visual/Character/AnimationPlayer

@onready var entity := NetwEntity.of(self)
@onready var clock := Netw.session(self).clock
@onready var shoot := Netw.action(hit)

@export var crosshair: TextureRect
@export var skins: Array[Texture2D] = []

var skin := 0


func _init():
	var e := Netw.configure_entity(self)
	e.initial_controller = NetwEntity.INITIAL_PLAYER
	e.on_controller_disconnect = NetwEntity.DISCONNECT_DESPAWN
	e.display.visual_root = ^"Visual"
	Netw.configure_property(self, &"position").broadcast().on_spawn() \
			.interpolate(NetwInterpolate.new().lerp())
	Netw.configure_property(self, &"quaternion").broadcast() \
			.interpolate(NetwInterpolate.new().slerp())
	Netw.configure_property(self, &"velocity").broadcast()
	Netw.configure_property(self, &"previously_floored").broadcast()
	Netw.configure_property(self, &"weapon_index").broadcast()
	Netw.configure_property(self, &"shots").broadcast()
	Netw.configure_rpc(damage).call_local()

# Functions


func _ready():
	$Visual/Character/Root/Skeleton3D/characterMedium.material_override.albedo_texture = skins[skin]

	if not entity.is_controlled_locally:
		$Head/Camera/SubViewportContainer.queue_free()
		$HUD.queue_free()
		sound_footsteps.stream_paused = true
		set_process(false)
		set_process_input(false)
		return

	$Visual.hide()
	camera.make_current()
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED

	weapon = weapons[weapon_index] # Weapon must never be nil
	initiate_change_weapon(weapon_index)


func _process(delta):
	# Handle functions
	handle_controls(delta)
	handle_gravity(delta)

	# Movement

	var applied_velocity: Vector3

	movement_velocity = transform.basis * movement_velocity # Move forward

	applied_velocity = velocity.lerp(movement_velocity, delta * 10)
	applied_velocity.y = -gravity

	velocity = applied_velocity
	move_and_slide()

	# Rotation 
	container.position = lerp(container.position, container_offset - (basis.inverse() * applied_velocity / 30), delta * 10)

	# Movement sound

	sound_footsteps.stream_paused = true

	if is_on_floor():
		if abs(velocity.x) > 1 or abs(velocity.z) > 1:
			sound_footsteps.stream_paused = false

	# Landing after jump or falling

	camera.position.y = lerp(camera.position.y, 0.0, delta * 5)

	if is_on_floor() and gravity > 1 and !previously_floored: # Landed
		Audio.play("sounds/land.ogg")
		camera.position.y = -0.1

	previously_floored = is_on_floor()

	# Falling/respawning

	if position.y < -10:
		position = spawn_position


func _physics_process(_delta):
	if !previously_floored:
		animation.play("jump")
	elif abs(velocity.x) > 1 or abs(velocity.z) > 1:
		animation.play("run")
	else:
		animation.play("idle")

# Mouse movement


func _input(event):
	if event is InputEventMouseMotion and mouse_captured:
		input_mouse = event.relative / mouse_sensitivity
		handle_rotation(event.relative.x, event.relative.y, false)


func handle_controls(delta):
	# Mouse capture
	if Input.is_action_just_pressed("mouse_capture"):
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
		mouse_captured = true

	if Input.is_action_just_pressed("mouse_capture_exit"):
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		mouse_captured = false

		input_mouse = Vector2.ZERO

	# Movement
	var input := Input.get_vector("move_left", "move_right", "move_forward", "move_back")
	movement_velocity = Vector3(input.x, 0, input.y).normalized() * movement_speed

	# Handle Controller Rotation
	var rotation_input := Input.get_vector("camera_right", "camera_left", "camera_down", "camera_up")
	if rotation_input:
		handle_rotation(rotation_input.x, rotation_input.y, true, delta)

	# Shooting

	action_shoot()

	# Jumping

	if Input.is_action_just_pressed("jump"):
		if jumps_remaining:
			action_jump()

	# Weapon switching

	action_weapon_toggle()

# Camera rotation


func handle_rotation(xRot: float, yRot: float, isController: bool, delta: float = 0.0):
	if isController:
		rotation_target -= Vector3(-yRot, -xRot, 0).limit_length(1.0) * gamepad_sensitivity
		rotation_target.x = clamp(rotation_target.x, deg_to_rad(-90), deg_to_rad(90))
		camera.rotation.x = lerp_angle(camera.rotation.x, rotation_target.x, delta * 25)
		rotation.y = lerp_angle(rotation.y, rotation_target.y, delta * 25)
	else:
		rotation_target += (Vector3(-yRot, -xRot, 0) / mouse_sensitivity)
		rotation_target.x = clamp(rotation_target.x, deg_to_rad(-90), deg_to_rad(90))
		camera.rotation.x = rotation_target.x
		rotation.y = rotation_target.y

# Handle gravity


func handle_gravity(delta):
	gravity += 20 * delta

	if gravity < 0 and is_on_ceiling():
		gravity = 0

	if gravity > 0 and is_on_floor():
		jumps_remaining = number_of_jumps
		gravity = 0

# Jumping


func action_jump():
	Audio.play("sounds/jump_a.ogg, sounds/jump_b.ogg, sounds/jump_c.ogg")
	gravity = -jump_strength
	jumps_remaining -= 1

# Shooting


func action_shoot():
	if Input.is_action_pressed("shoot"):
		if !blaster_cooldown.is_stopped():
			return # Cooldown for shooting

		shots += 1

		blaster_cooldown.start(weapon.cooldown)

		# Shoot the weapon, amount based on shot count

		for n in weapon.shot_count:
			raycast.target_position.x = randf_range(-weapon.spread, weapon.spread)
			raycast.target_position.y = randf_range(-weapon.spread, weapon.spread)

			raycast.force_raycast_update()

			if !raycast.is_colliding():
				continue # Don't create impact when raycast didn't hit

			var collider = raycast.get_collider()

			# Hitting an enemy

			if collider.has_method("damage"):
				shoot.request(clock.tick - clock.display_offset, raycast.global_position, raycast.get_collision_point(), weapon_index)

			# Creating an impact animation

			var impact = preload("res://examples/fps/objects/impact.tscn")
			var impact_instance = impact.instantiate()

			impact_instance.play("shot")

			get_tree().root.add_child(impact_instance)

			impact_instance.position = raycast.get_collision_point() + (raycast.get_collision_normal() / 10)
			impact_instance.look_at(camera.global_transform.origin, Vector3.UP, true)

		var knockback = random_vec2(weapon.min_knockback, weapon.max_knockback)
		# print('knockback', knockback)
		container.position.z += 0.25 # Knockback of weapon visual
		camera.rotation.x += knockback.x # Knockback of camera
		rotation.y += knockback.y
		rotation_target.x += knockback.x
		rotation_target.y += knockback.y
		movement_velocity += Vector3(0, 0, weapon.knockback) # Knockback

# Toggle between available weapons (listed in 'weapons')


func action_weapon_toggle():
	if Input.is_action_just_pressed("weapon_toggle"):
		weapon_index = wrap(weapon_index + 1, 0, weapons.size())
		initiate_change_weapon(weapon_index)

		Audio.play("sounds/weapon_change.ogg")

# Initiates the weapon changing animation (tween)


func initiate_change_weapon(index):
	weapon_index = index

	tween = get_tree().create_tween()
	tween.set_ease(Tween.EASE_OUT_IN)
	tween.tween_property(container, "position", container_offset - Vector3(0, 1, 0), 0.1)
	tween.tween_callback(change_weapon) # Changes the model

# Switches the weapon model (off-screen)


func change_weapon():
	weapon = weapons[weapon_index]

	# Step 1. Remove previous weapon model(s) from container

	for n in container.get_children():
		container.remove_child(n)

	# Step 2. Place new weapon model in container

	var weapon_model = weapon.model.instantiate()
	container.add_child(weapon_model)

	weapon_model.position = weapon.position
	weapon_model.rotation_degrees = weapon.rotation

	# Step 3. Set model to only render on layer 2 (the weapon camera)

	for child in weapon_model.find_children("*", "MeshInstance3D"):
		child.layers = 2

	# Set weapon data

	raycast.target_position = Vector3(0, 0, -1) * weapon.max_distance
	crosshair.texture = weapon.crosshair


func damage(amount):
	health -= amount

	if health < 0:
		health = 100 # Reset when out of health
		position = spawn_position


func hit(ctx: NetwActionContext, from: Vector3, to: Vector3, index: int):
	var result: Dictionary = Netw.rewind(
		entity.scene.entities,
		ctx.view_tick,
		func():
			var query := PhysicsRayQueryParameters3D.create(from, from + from.direction_to(to) * weapons[index].max_distance)
			query.collide_with_areas = true
			query.exclude = [get_rid()]
			return get_world_3d().direct_space_state.intersect_ray(query)
	)

	if !result.is_empty() and result.collider.has_method("damage"):
		Netw.rpc(result.collider.damage, weapons[index].damage)


# Create a random knockback vector
static func random_vec2(_min: Vector2, _max: Vector2) -> Vector2:
	var _sign = -1 if randi() % 2 == 0 else 1
	return Vector2(randf_range(_min.x, _max.x), randf_range(_min.y, _max.y) * _sign)
