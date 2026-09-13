extends Label

const SHOWN: Array[String] = [
	"sent_bytes",
	"received_bytes",
	"predict_corrections",
]

@onready var clock: NetwClockHandle = Netw.clock(self)
@onready var session: NetwSessionHandle = Netw.session(self)


func _process(_delta: float) -> void:
	var role := "Server" if multiplayer.is_server() else "Client"
	var lines := PackedStringArray(
		[
			"%s - %d" % [role, multiplayer.get_unique_id()],
			"FPS: %d" % Performance.get_monitor(Performance.TIME_FPS),
			"tick: %d" % clock.tick,
		],
	)
	var stats: Dictionary = session.stats
	for key: String in SHOWN:
		lines.append("%s: %s" % [key, stats.get(key, 0)])
	text = "\n".join(lines)
