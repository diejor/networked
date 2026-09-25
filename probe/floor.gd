extends SceneTree

const PLAIN := preload("res://probe/cube_plain.tscn")

var passes := 0


func _process(_delta: float) -> bool:
	if passes == 3:
		return true
	passes += 1
	var rows := int(OS.get_environment("PLAYGROUND_ROWS"))
	var material := StandardMaterial3D.new()
	var world := Node3D.new()
	var started := Time.get_ticks_usec()
	for at in rows * rows:
		var cube: Node3D = PLAIN.instantiate()
		cube.name = "Cube_%d" % at
		cube.position = Vector3(at % rows, 0.0, at / rows) * 0.65
		cube.get_node(^"Visual").material_override = material
		world.add_child(cube)
	var built := Time.get_ticks_usec()
	root.add_child(world)
	var mounted := Time.get_ticks_usec()
	print("FLOOR cubes=%d inside=%s construct_ms=%.1f mount_ms=%.1f" % [
		rows * rows, world.is_inside_tree(), (built - started) / 1000.0,
		(mounted - built) / 1000.0])
	world.queue_free()
	return false
