extends Node2D

@onready var in_lobby: InLobby = %InLobby
@onready var gamestate: BomberGamestate = Netw.service(
	self, BomberGamestate,
) as BomberGamestate


func _init() -> void:
	Netw.configure_multiplayer_scene(self).labeled(&"Lobby")


func _ready() -> void:
	in_lobby.start_pressed.connect(gamestate.begin_game)
