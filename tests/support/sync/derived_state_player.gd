## A minimal node that declares a state set and an input set through
## [method Netw.configure_property], the derived-set counterpart of the
## proxy-family player the loopback rigs build. Its script marks drive
## [method NetwSyncPipeline.register_derived].
extends Node2D

func _init() -> void:
	Netw.configure_property(self, &"position").state()
	Netw.configure_property(self, &"rotation").input()
