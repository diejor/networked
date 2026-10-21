extends CanvasLayer

@onready var status: Label = %StatusLabel
@onready var in_lobby: InLobby = %InLobby
@onready var session: NetwSession = Netw.session(self)


func _ready() -> void:
	session.scene_ready.connect(on_scene_ready)

	var gamestate := session.service(BomberGamestate) as BomberGamestate
	gamestate.game_error.connect(set_status)
	in_lobby.start_pressed.connect(gamestate.begin_game)

	status.visible = false


func on_scene_ready(_scene: NetwScene) -> void:
	set_status("")


func set_status(text: String) -> void:
	status.text = text
	status.visible = not text.is_empty()
