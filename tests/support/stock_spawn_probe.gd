## Stock-purity probe for the [MultiplayerSpawner] conformance suite.
##
## Deliberately touches no Networked API: its spawn state rides a plain
## [MultiplayerSynchronizer] [SceneReplicationConfig] spawn property, exactly
## as an unmodified stock project authors it. Snapshots the value at
## [method Node._enter_tree] so the suite can assert state applies pre-tree
## on receivers.
class_name StockSpawnProbe
extends Node2D

## The packed probe scene path for the current test, read by [StockWorld] to
## register the spawnable scene and build custom spawns.
static var packed_scene_path := ""

## Carried by the synchronizer's replication_config as a spawn property.
var stock_value := ""

## Set by the custom spawn function from its data argument.
var custom_data := ""

## Snapshot of [member stock_value] taken during [method Node._enter_tree].
var enter_tree_value := ""

## When true on the authority, _ready overwrites stock_value, the row-14
## timing case (native reads spawn state after ready).
var mutate_in_ready := false


func _enter_tree() -> void:
	enter_tree_value = stock_value


func _ready() -> void:
	if mutate_in_ready and multiplayer.is_server():
		stock_value = "ready-mutated"
