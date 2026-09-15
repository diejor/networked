extends Node3D

@onready var camera: Camera3D = $Camera

var target: Vehicle


func _ready() -> void:
	var scene: NetwSceneHandle = Netw.scene(self)
	scene.player_entered.connect(take_local_car)
	for player: NetwEntity in scene.local_players:
		take_local_car(player)


func take_local_car(player: NetwEntity) -> void:
	if player.is_controlled_locally:
		target = player.owner as Vehicle


func _physics_process(delta: float) -> void:
	if not is_instance_valid(target):
		return

	self.position = self.position.lerp(target.get_vehicle_position(), delta * 4)

	var speed_factor := clampf(absf(target.linear_speed), 0.0, 1.0)
	var target_z := remap(speed_factor, 0.0, 1.0, 10, 20)

	camera.position.z = lerpf(camera.position.z, target_z, delta * 0.5)
