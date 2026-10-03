extends RichTextLabel

@onready var countdown: RichTextLabel = $"../CountDown"
@onready var session: NetwSession = Netw.session(self)

var game: RocketGame


func _ready() -> void:
	set_process(false)
	session.scene_ready.connect(on_scene_ready)


func on_scene_ready(arena: NetwScene) -> void:
	game = arena.root.get_node(^"game")
	arena.root.tree_exited.connect(on_arena_closed)
	set_process(true)


func on_arena_closed() -> void:
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
