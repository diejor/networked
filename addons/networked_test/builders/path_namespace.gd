## Allocates and tracks unique in-memory resource paths for builders.
##
## Tracks all generated resources to clean them up from Godot's memory-only
## [ResourceCache] between test suites or test cases, ensuring no memory leak
## growth or collision across runs.
class_name NetwPathNamespace
extends Object

static var _counter: int = 0
static var _resources: Array[Resource] = []


## Allocates the next unique path for the given category and hint.
##
## Returns a path in the format
## [code]res://_netwtest/<category>/<seq>/<hint>.tscn[/code].
static func next_path(category: String, hint: String) -> String:
	_counter += 1
	return "res://_netwtest/%s/%d/%s.tscn" % [category, _counter, hint]


## Registers a [param resource] to be tracked and eventually swept.
static func register_resource(resource: Resource) -> void:
	if not _resources.has(resource):
		_resources.append(resource)


## Resets the allocator and evicts all tracked resources from the cache.
##
## For each tracked resource, calls [method Resource.take_over_path] with an
## empty string to cleanly evict it from Godot's [ResourceCache].
static func reset() -> void:
	for res in _resources:
		if is_instance_valid(res):
			res.take_over_path("")
	_resources.clear()

	# Safely reset builder counters directly using global class names.
	LevelBuilder.reset_counter()
	PlayerBuilder.reset_counter()
