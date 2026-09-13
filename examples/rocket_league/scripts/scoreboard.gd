extends RichTextLabel

@onready var countdown: RichTextLabel = $"../CountDown"

var game: RocketGame


func _ready() -> void:
	set_process(false)
	var session: NetwSessionHandle = Netw.session(self)
	session.scene_live.connect(on_scene_live)
	session.ended.connect(on_session_ended)
	session.disconnected.connect(on_session_ended)


func on_scene_live(scene: NetwSceneHandle) -> void:
	if scene.label != &"Arena":
		return
	game = scene.root.get_node(^"game|0")
	set_process(true)


func on_session_ended() -> void:
	set_process(false)
	text = ""
	countdown.text = ""


func _process(_delta: float) -> void:
	text = "[outline_size=5][outline_color=black][color=red]%d[/color] - [color=blue]%d[/color][/outline_color][/outline_size]" % [
		game.score_red,
		game.score_blue,
	]
	var left := game.countdown_seconds()
	countdown.text = "" if left <= 0.0 \
	else "[outline_size=10][outline_color=black]%d[/outline_color][/outline_size]" % ceili(left)
