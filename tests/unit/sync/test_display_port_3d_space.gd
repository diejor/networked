## [NetwDisplayPort]'s three-dimensional global-space writes, which need a tree.
##
## The port's two-dimensional arms are lawed natively, because a [Node2D]
## composes its global transform from its parents whether or not it is inside a
## tree. A [Node3D] does not: [method Node3D.get_global_transform] refuses
## outside the tree, and the native tier's runner executes before its own root
## enters one. So the three-dimensional arms are owed a tier that has a tree,
## and this is it.
class_name TestDisplayPort3DSpace
extends NetwTestSuite

var _holder: Node3D
var _body: Node3D
var _visual: Node3D


func before_test() -> void:
	_holder = Node3D.new()
	_body = Node3D.new()
	_visual = Node3D.new()
	add_child(_holder)
	auto_free(_holder)
	_holder.add_child(_body)
	_body.add_child(_visual)


func _port(target_prop: StringName, source_prop: StringName) -> NetwDisplayPort:
	var port := NetwDisplayPort.new()
	port.bind(_visual, _body)
	port.declare(target_prop, source_prop, true)
	return port


func test_position_leaves_the_hosts_parent_frame() -> void:
	_holder.position = Vector3(0.0, 10.0, 0.0)

	var port := _port(&"position", &"position")

	assert_int(port.write(Vector3(0.0, 2.0, 0.0))) \
			.is_equal(NetwDisplayPort.WRITE_GLOBAL)
	assert_vector(_visual.global_position).is_equal_approx(
		Vector3(0.0, 12.0, 0.0),
		Vector3.ONE * 0.001,
	)


func test_rotation_is_converted_rather_than_refused() -> void:
	_holder.rotation = Vector3(0.0, PI * 0.5, 0.0)

	var port := _port(&"rotation", &"rotation")

	assert_int(port.write(Vector3(0.0, PI * 0.25, 0.0))) \
			.is_equal(NetwDisplayPort.WRITE_GLOBAL)
	assert_vector(_visual.global_rotation).is_equal_approx(
		Vector3(0.0, PI * 0.75, 0.0),
		Vector3.ONE * 0.001,
	)


func test_a_source_already_global_passes_through_the_parent() -> void:
	_holder.position = Vector3(0.0, 10.0, 0.0)

	var port := _port(&"position", &"global_position")

	assert_int(port.write(Vector3(0.0, 2.0, 0.0))) \
			.is_equal(NetwDisplayPort.WRITE_GLOBAL)
	assert_vector(_visual.global_position).is_equal_approx(
		Vector3(0.0, 2.0, 0.0),
		Vector3.ONE * 0.001,
	)


func test_a_channel_with_no_global_setter_is_refused() -> void:
	var port := _port(&"scale", &"scale")

	assert_int(port.write(Vector3(2.0, 2.0, 2.0))) \
			.is_equal(NetwDisplayPort.WRITE_REFUSED)
	assert_vector(_visual.scale).is_equal_approx(
		Vector3(2.0, 2.0, 2.0),
		Vector3.ONE * 0.001,
	)
