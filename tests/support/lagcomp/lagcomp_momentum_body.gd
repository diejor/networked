## Forecast body with a server-perturbable scalar the sim never writes, for the
## restore-participation scenarios.
##
## [member boost] joins the state set but the closed-form step never touches it,
## so whatever a correction restores or preserves stays visible to a test
## instead of being recomputed away next tick. It stands in for the contractive
## fields a game marks [member PredictionComponent.teleport_only_restore_fields].
class_name LagCompMomentumBody
extends LagCompForecastBody

## Replicated scalar the sim never writes, so a restore's effect persists.
var boost: float = 0.0


func _init() -> void:
	super()
	Netw.configure_property(self, &"boost").state()
