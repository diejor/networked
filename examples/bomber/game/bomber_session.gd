extends Node

func _init() -> void:
	Netw.configure_session(self).app(&"7x283tsfmy1xpr4")
	Netw.configure_clock(self).ticks_per_second(15)
