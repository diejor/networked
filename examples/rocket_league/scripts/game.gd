class_name RocketGame
extends Node

const KICKOFF_DELAY := 3.5

@onready var clock: NetwClockHandle = Netw.clock(self)

var score_red := 0
var score_blue := 0
var kickoff_tick := 0


func _init() -> void:
	Netw.configure_property(self, &"score_red").state().on_spawn()
	Netw.configure_property(self, &"score_blue").state().on_spawn()
	Netw.configure_property(self, &"kickoff_tick").state().on_spawn()


func _ready() -> void:
	if multiplayer.is_server():
		queue_kickoff()
		Netw.session(self).participant_joined.connect(_on_participant_joined)


func rule_goal(team: int, _tick: int) -> void:
	if team == 0:
		score_red += 1
	else:
		score_blue += 1
	queue_kickoff()


func queue_kickoff() -> void:
	kickoff_tick = clock.tick + seconds_to_ticks(KICKOFF_DELAY)


func countdown_seconds() -> float:
	return maxf(0.0, kickoff_tick - clock.tick) / tickrate()


func seconds_to_ticks(seconds: float) -> int:
	return int(seconds * tickrate())


func tickrate() -> float:
	return clock.param(NetwMultiplayer.CLOCK_PARAM_TICKRATE)


func _on_participant_joined(_participant: NetwParticipant) -> void:
	queue_kickoff()
