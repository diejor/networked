class_name NetwEmbeddingWorld
extends RefCounted

const _PROBE_SCENE := preload("res://tests/support/probe.tscn")


func host() -> NetwMultiplayer:
	return null


func add_client(_username: String) -> NetwMultiplayer:
	return null


func declare_initial_scene(_scene: PackedScene) -> void:
	pass


func mount_clock() -> void:
	pass


func pump_until(_cond: Callable, _timeout_ms: int = 3000) -> bool:
	return false


func provider() -> String:
	return "base"


func spawn_probe(name: String) -> int:
	var host_api := await host()
	var node := _PROBE_SCENE.instantiate()
	node.name = name
	host_api.entity_replicate(node)
	host_api.root.add_child(node)
	return NetwEntity.of(node).route


func dispose() -> void:
	pass
