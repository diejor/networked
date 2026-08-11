## Bootstraps the swarm example's session and starts both halves once it is
## online.
##
## The example carries no scenes and no player entities, so the whole
## bootstrap is: declare the tables before anything connects, then let the
## server simulate and the client render.
extends Node3D

@onready var _server: SwarmServer = %SwarmServer
@onready var _view: SwarmView = %SwarmView

var api: NetwMultiplayer:
	get:
		return multiplayer as NetwMultiplayer


func _enter_tree() -> void:
	# A wire id is the name-sorted position among sealed tables, so both peers
	# must have declared the same set before either sends a frame.
	SwarmTables.declare_all()
	var session := NetwSessionConfig.new()
	session.app_id = &"netw-example-swarm"
	api.object_configuration_add(self, session)


func _ready() -> void:
	api.session_entered.connect(_on_session_entered)


func _on_session_entered() -> void:
	_view.start()
	_server.start()
