class_name DuckTypeHelper
extends Object

static func get_duck(obj: Object, prop: String, default: Variant = null) -> Variant:
	if not obj: return default
	if prop in obj: return obj.get(prop)
	
	var pascal_prop := prop.to_pascal_case()
	if pascal_prop in obj: return obj.get(pascal_prop)
	
	return default

static func set_duck(obj: Object, prop: String, value: Variant) -> void:
	if not obj: return
	if prop in obj: 
		obj.set(prop, value)
		return
		
	var pascal_prop := prop.to_pascal_case()
	if pascal_prop in obj: 
		obj.set(pascal_prop, value)

static func call_duck(obj: Object, method: String, args: Array = []) -> Variant:
	if not obj: return null
	if obj.has_method(method): return obj.callv(method, args)
	var pascal := method.to_pascal_case()
	if obj.has_method(pascal): return obj.callv(pascal, args)
	return null

static func create_resource(is_csharp_mode: bool) -> Variant:
	if is_csharp_mode and ClassDB.class_exists("CSharpScript"):
		var cs_script = load("uid://bj2k7avaeljdf")
		if cs_script: return cs_script.new()
	return SceneNodePath.new()
