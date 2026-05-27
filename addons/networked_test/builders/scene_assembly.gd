## Utility for safely composing and packing dynamic node trees in memory.
##
## Hard-wrapped to 80 columns.
class_name SceneAssembly
extends Object


## Attaches a [param child] node to a [param parent] and sets its owner.
##
## Recursively sets the owner of the child node tree to [param child_owner]
## to ensure serialization readiness when packing the scene.
## [br][br]
## [b]Note:[/b] [param child_owner] is required because Godot's
## [method PackedScene.pack] ignores any node that is not owned by the packed
## root.
static func attach(parent: Node, child: Node, child_owner: Node) -> Node:
	parent.add_child(child)
	child.owner = child_owner
	_set_owner_recursive(child, child_owner)
	return child


## Packs a live [param root] node hierarchy into a [PackedScene].
##
## Asserts that the packed result is valid and that the instantiated child count
## matches the original [param root]'s recursive child count to catch owner
## omissions. Registers the scene path in memory via [method Resource.take_over_path].
static func pack_with_path(root: Node, path: String) -> PackedScene:
	_strip_netw_entity_meta_recursive(root)
	var packed := PackedScene.new()
	var err := packed.pack(root)
	assert(err == OK, "SceneAssembly: Failed to pack root node.")
	var instantiated := packed.instantiate()
	var live_count := _count_children_recursive(root)
	var packed_count := _count_children_recursive(instantiated)
	instantiated.free()
	assert(
		live_count == packed_count,
		"SceneAssembly: Child count mismatch. " + \
		"Expected %d, got %d. Did you forget to set owner?" % \
		[live_count, packed_count]
	)
	packed.take_over_path(path)
	return packed


# Recursively strips netw_entity metadata from the node tree.
#
# Why: SpawnerComponent assigns netw_entity in _ready; packing would
# serialize active/stale runtime IDs, breaking re-instantiation.
static func _strip_netw_entity_meta_recursive(node: Node) -> void:
	if node.has_meta(&"netw_entity"):
		node.remove_meta(&"netw_entity")
	for child in node.get_children():
		_strip_netw_entity_meta_recursive(child)



# Recursively sets the owner of the [param node] tree to [param new_owner].
static func _set_owner_recursive(node: Node, new_owner: Node) -> void:
	if not node.scene_file_path.is_empty():
		return
	for child in node.get_children():
		child.owner = new_owner
		_set_owner_recursive(child, new_owner)


# Recursively counts all child nodes of a given [param node].
static func _count_children_recursive(node: Node) -> int:
	var count := node.get_child_count()
	for child in node.get_children():
		count += _count_children_recursive(child)
	return count
