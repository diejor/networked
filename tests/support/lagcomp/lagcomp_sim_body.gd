## Closed-form predicted-entity root for the real-node lag-comp scenarios.
##
## The entity root defines the [code]_network_tick[/code] contract that
## [member PredictionComponent.simulate] auto-binds, so the predicting client,
## the consuming server, and every reconciliation replay run the same step. The
## body stays closed-form (no [method CharacterBody2D.move_and_slide]) so every
## scenario can compute the expected position analytically and assert exact
## equality. Physics-replay fidelity is a separate spike
## ([code]test_spike_kinematic_replay.gd[/code]).
##
## [codeblock]
## motion / bombing  ──>  derived input-set payload  (scripted by the rig)
## _network_tick     ──>  position += motion * SPEED * dt   (closed-form)
##                        fire_count += 1   only on a fresh bombing tick
## [/codeblock]
class_name LagCompSimBody
extends Node2D

## Per-tick motion request, the input set's [code]motion[/code] field. The rig
## writes it on the client body to script input.
var motion: Vector2 = Vector2.ZERO

## One-shot fire request, the input set's [code]bombing[/code] field. The rig
## pulses it for a single tick.
var bombing: bool = false

## Times a fresh bombing tick fired, never incremented by a replay pass.
var fire_count: int = 0


# Declares the derived state and input sets from the body's own script: position
# is the server-authored predicted state, motion and bombing the controller-authored
# input. Both peers run this script, so both derive the same set schema.
func _init() -> void:
	Netw.configure_property(self, &"position").state()
	Netw.configure_property(self, &"motion").input()
	Netw.configure_property(self, &"bombing").input()


## Runs one closed-form simulation step over [param delta].
##
## Input is on the live node ([member motion], [member bombing]), applied by the
## framework before each call. [param is_fresh] is [code]true[/code] on the live
## predict or consume pass and [code]false[/code] on a reconciliation replay, so
## a one-shot fire counts once.
func _network_tick(
		delta: float,
		_tick: int,
		is_fresh: bool,
) -> void:
	var m: Vector2 = motion
	position = ClosedFormSim.integrate(position, { &"mx": m.x, &"my": m.y }, delta)
	if is_fresh and bombing:
		fire_count += 1
