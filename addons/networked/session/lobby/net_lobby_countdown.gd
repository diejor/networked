## Server-driven countdown that ticks once per second.
##
## Obtain via [method NetLobbyContext.start_countdown] — do not construct directly.
## Clients do not receive a return value; they listen to
## [signal NetLobbyContext.countdown_started] and the subsequent
## [signal NetLobbyContext.countdown_tick] / [signal NetLobbyContext.countdown_finished]
## signals, which are broadcast automatically.
## [codeblock]
## # Server:
## var cd := ctx.start_countdown(10)
## await cd.finished
## start_match()
##
## # Client (connect before the server starts the countdown):
## ctx.countdown_started.connect(func(n): $Timer.text = str(n))
## ctx.countdown_tick.connect(func(n): $Timer.text = str(n))
## ctx.countdown_finished.connect(start_match)
## [/codeblock]
class_name NetLobbyCountdown
extends RefCounted

## Emitted each second with the remaining seconds (including 0 at the very end).
signal tick(seconds_left: int)
## Emitted when the countdown reaches zero.
signal finished()
## Emitted when [method cancel] is called before the countdown reaches zero.
signal cancelled()

var _lobby_ref: WeakRef
var _seconds_left: int
var _running: bool = false


func _init(lobby: Lobby, seconds: int) -> void:
	_lobby_ref = weakref(lobby)
	_seconds_left = seconds


## Returns [code]true[/code] if the countdown is actively ticking.
func is_running() -> bool:
	return _running


## Returns the number of seconds remaining.
func get_seconds_left() -> int:
	return _seconds_left


## Cancels the countdown and emits [signal cancelled].
## Does nothing if the countdown is not running.
func cancel() -> void:
	if not _running:
		return
	_running = false
	cancelled.emit()


## Starts ticking. Called internally by [method NetLobbyContext.start_countdown].
func _start() -> void:
	_running = true
	_schedule_tick()


func _schedule_tick() -> void:
	var lobby := _lobby_ref.get_ref() as Lobby
	if not is_instance_valid(lobby) or not lobby.is_inside_tree():
		_running = false
		return
	lobby.get_tree().create_timer(1.0).timeout.connect(_on_tick, CONNECT_ONE_SHOT)


func _on_tick() -> void:
	if not _running:
		return
	_seconds_left -= 1
	tick.emit(_seconds_left)
	if _seconds_left <= 0:
		_running = false
		finished.emit()
	else:
		_schedule_tick()
