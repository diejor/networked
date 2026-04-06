class_name CacheState
extends RefCounted

var scene_path: String = ""
var node_path: String = ""
var mod_time: int = 0
var is_broken: bool = true
var dynamic_class: String = "Node"
var warning_msg: String = ""

func is_valid(s_path: String, n_path: String, time: int) -> bool:
	return scene_path == s_path and node_path == n_path and mod_time == time

func update(s_path: String, n_path: String, time: int, broken: bool, dyn_class: String) -> void:
		scene_path = s_path
		node_path = n_path
		mod_time = time
		is_broken = broken
		dynamic_class = dyn_class
