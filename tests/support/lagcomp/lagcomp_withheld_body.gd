## Forecast body whose perturbable scalar is declared teleport-only, for the
## restore-participation and escalation scenarios.
##
## [member boost] joins the state set, the closed-form step never writes it,
## and its [method NetwScriptModel.PropertyConfig.teleport_only] mark is the
## one source of the withheld set. A rig that needs a field a sub-teleport
## recovery leaves on the predicted body uses this body, while
## [LagCompMomentumBody] carries the same scalar unmarked for the rigs that
## expect it restored.
class_name LagCompWithheldBody
extends LagCompForecastBody

## Replicated scalar the sim never writes, withheld below the teleport tier.
var boost: float = 0.0


func _init() -> void:
	super()
	Netw.configure_property(self, &"boost").state().teleport_only()
