## Test probe for the [method Netw.replicate] and [method Netw.spawn] verbs.
##
## Records identity snapshots at [method Node._enter_tree] and
## [method Node._ready] so a suite can assert the I1/I2 pre-tree contract on
## every peer, and sets its multiplayer authority from the stamped
## [member NetwEntity.peer_id] in [method Node._enter_tree], the exact move
## that used to trip the native pending-spawn error.
class_name NetwSpawnProbe
extends Node2D

## Route of the last despawn hook invocation on this peer, reset by suites.
static var last_despawn_hook_route := 0

## Spawn-state property carried on the SPAWN frame.
var marker := ""

## Set by the spawn-function recipe from its arguments on every peer.
var fn_tier := 0

## Identity snapshot taken during [method Node._enter_tree].
var enter_tree_report := { }

## Identity snapshot taken during [method Node._ready].
var ready_report := { }


func _init() -> void:
	Netw.configure_property(self, &"marker").on_spawn()
	Netw.configure_despawn(self).before_removal(_record_despawn)


func _enter_tree() -> void:
	var entity := NetwEntity.of(self)
	if entity and entity.peer_id != 0:
		# SMELL(authority-pin): the probe mirrors arm()'s represented-peer authority
		# mapping by hand, standing in for a spawn this fixture never runs.
		set_multiplayer_authority(entity.peer_id)
	enter_tree_report = _report()


func _ready() -> void:
	ready_report = _report()


func _record_despawn() -> void:
	var entity := NetwEntity.of(self)
	last_despawn_hook_route = entity.route if entity else -1


func _report() -> Dictionary:
	var entity := NetwEntity.of(self)
	return {
		"route": entity.route if entity else 0,
		"entity_id": entity.entity_id if entity else &"",
		"peer_id": entity.peer_id if entity else -1,
		"controller": entity.controller if entity else -1,
		"marker": marker,
		"fn_tier": fn_tier,
		"authority": get_multiplayer_authority(),
	}
