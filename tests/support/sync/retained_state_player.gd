## A node whose state set spans both lanes: a volatile field that resends every
## tick and a retained field that rides reliably only when it changes.
##
## The two lanes of one set are what this exists to drive. A rig that declared
## only one of them proves nothing about the other, and the two share a route
## and an ordinal.
extends Node2D

var stunned: bool = false

func _init() -> void:
	Netw.configure_property(self, &"position").state()
	Netw.configure_property(self, &"stunned").state().retained()
