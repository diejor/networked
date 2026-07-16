## Minimal masked-lane state-synced entity root for the P5-MASKED loss suite.
##
## Declares a server-authored [code]position[/code] state set through
## [method Netw.configure_property] with [method NetwScriptModel.PropertyConfig.masked]
## instead of [StateSyncBody]'s shared-broadcast default, so the pump computes
## and sends a per-recipient masked frame rather than one shared row.
class_name MaskedStateSyncBody
extends Node2D


func _init() -> void:
	Netw.configure_property(self, &"position").state().masked()
