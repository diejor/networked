## Minimal state-synced entity root for the rewind and timeline suites.
##
## Declares a server-authored [code]position[/code] state set through
## [method Netw.configure_property], with no input and no prediction, so state-set
## presence alone drives the lag compensation timeline registration. The scenarios
## drive its position directly.
class_name StateSyncBody
extends Node2D


func _init() -> void:
	Netw.configure_property(self, &"position").state()
