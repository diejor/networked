extends Label

## Shown and hidden together with this label.
@export var icon: Control

var user: DiscordProfile:
	set(value):
		user = value
		_on_timer_timeout()


func _ready() -> void:
	$Timer.timeout.connect(_on_timer_timeout)
	_on_timer_timeout()


func _on_timer_timeout() -> void:
	visible = user != null and user.playing_since > 0.0
	icon.visible = visible
	if not visible:
		return

	var seconds := maxi(
		int(Time.get_unix_time_from_system() - user.playing_since),
		0,
	)
	text = "%d:%02d" % [floori(seconds / 60.0), seconds % 60]
	if seconds >= 3600:
		text = "%d:%02d:%02d" % [
			floori(seconds / 3600.0),
			floori(seconds / 60.0) % 60,
			seconds % 60,
		]
