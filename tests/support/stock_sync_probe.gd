## Stock-purity probe for the [MultiplayerSynchronizer] conformance suite.
##
## Deliberately touches no Networked API. A plain [MultiplayerSynchronizer]
## child, built by the suite, carries a sync property, a watch property, and a
## spawn property on a [SceneReplicationConfig] exactly as an unmodified stock
## project authors them. Snapshots the spawn value at [method Node._enter_tree]
## so the suite can assert spawn state applies pre-tree on receivers.
class_name StockSyncProbe
extends Node2D

## The packed probe scene path for the current test, read by [StockSyncWorld]
## to register the spawnable scene.
static var packed_scene_path := ""

## Additional packed probe scene paths [StockSyncWorld] also registers, for
## suites that spawn more than one probe shape in a session.
static var extra_scene_paths: Array[String] = []

## Carried by the synchronizer as REPLICATION_MODE_ALWAYS (per-tick sync).
var synced_value := 0

## Carried by the synchronizer as REPLICATION_MODE_ON_CHANGE (reliable delta).
var watched_value := 0

## Carried by the synchronizer as a REPLICATION_MODE_NEVER spawn property.
var spawn_value := ""

## Snapshot of [member spawn_value] taken during [method Node._enter_tree].
var enter_tree_spawn_value := ""


func _enter_tree() -> void:
	enter_tree_spawn_value = spawn_value
