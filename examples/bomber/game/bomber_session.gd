extends Node

@export var clock_settings: NetwClockConfig

var api: NetwMultiplayer:
	get:
		return multiplayer


func _init() -> void:
	Netw.configure_session(self).app(&"7x283tsfmy1xpr4")


func _enter_tree() -> void:
	Netw.configure_clock(self, clock_settings).ticks_per_second(15)
