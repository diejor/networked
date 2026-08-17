## A derived-set node whose public state field is also a displayed track.
##
## The two declarations together are what makes this the one shape that reaches
## [code]DisplayCore._authors_derived_stream[/code]: the display runtime tracks
## [code]position[/code], and the only stream feeding it is a derived set rather
## than a synchronizer, so the display's authorship question can only be
## answered by asking the set.
extends Node2D

func _init() -> void:
	Netw.configure_property(self, &"position").state().interpolate(
		NetwInterpolate.new().lerp().smooth(0.05).to(&"position"),
	)
	Netw.configure_property(self, &"rotation").input()
