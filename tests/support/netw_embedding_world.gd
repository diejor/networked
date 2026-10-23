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


func register_probe(api: NetwMultiplayer) -> void:
	api.spawn_register_constructor(&"probe", make_probe, [TYPE_STRING])


func make_probe(probe_name: String) -> Node:
	var node := _PROBE_SCENE.instantiate()
	node.name = probe_name
	return node


func spawn_probe(name: String) -> int:
	var host_api := await host()
	var entity := host_api.spawn_registered(&"probe", [name])
	host_api.root.add_child(host_api.entity_get_node(entity))
	return host_api.entity_get_route(entity)


func dispose() -> void:
	pass
