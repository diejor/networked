## The controller-authored twin of [code]derived_display_player.gd[/code].
##
## Its displayed field rides a broadcast set rather than a state set, which is
## the kind whose authorship the write policy decides. A state set is the
## server's by definition, so it could never show the policy being read.
extends Node2D

func _init() -> void:
	Netw.configure_property(self, &"position").broadcast().controller() \
			.interpolate(
				NetwInterpolate.new().lerp().smooth(0.05).to(&"position"),
			)
	Netw.configure_property(self, &"rotation").input()
