extends SceneTree
## Drives a GDExtension reload cycle and checks the library still works after
## each step. In the editor a reload only happens on window focus, which a
## headless run never gets, so the cycle is driven through GDExtensionManager
## directly.
##
## Exits non-zero on the first failed check.

const MANIFEST := "res://addons/networked/bin/networked.gdextension"

var _failures := 0


func _initialize() -> void:
	_check("loaded at startup", GDExtensionManager.is_extension_loaded(MANIFEST))
	_check("works before the cycle", _round_trips())

	_check(
		"reload reports ok",
		GDExtensionManager.reload_extension(MANIFEST)
		== GDExtensionManager.LOAD_STATUS_OK,
	)
	_check("works after reload", _round_trips())

	_check(
		"unload reports ok",
		GDExtensionManager.unload_extension(MANIFEST)
		== GDExtensionManager.LOAD_STATUS_OK,
	)
	_check("classes are gone", not ClassDB.class_exists(&"NetwBitBufferWriter"))
	print("still mapped after unload: %s" % _still_mapped())

	_check(
		"load reports ok",
		GDExtensionManager.load_extension(MANIFEST)
		== GDExtensionManager.LOAD_STATUS_OK,
	)
	_check("works after re-enable", _round_trips())

	print("RELOAD_CYCLE failures=%d" % _failures)
	quit(1 if _failures > 0 else 0)


## Encodes and decodes through the native classes, resolved by name so that an
## unloaded extension is a failed check rather than a parse error.
func _round_trips() -> bool:
	if not ClassDB.class_exists(&"NetwBitBufferWriter"):
		return false
	var writer: Object = ClassDB.instantiate(&"NetwBitBufferWriter")
	writer.put_bits(0b1011, 4)
	writer.put_aligned_u32(4242)
	var reader: Object = ClassDB.instantiate(&"NetwBitBufferReader")
	reader.reset(writer.to_bytes())
	return reader.get_bits(4) == 0b1011 and reader.get_aligned_u32() == 4242


## Whether the library is still in the process image after unloading it. A
## shared object whose thread-local storage a live thread has touched cannot be
## unloaded, so dlclose reports success and leaves it mapped.
func _still_mapped() -> bool:
	var maps := FileAccess.open("/proc/self/maps", FileAccess.READ)
	if maps == null:
		return false
	return maps.get_as_text().contains("libnetworked")


func _check(what: String, passed: bool) -> void:
	if not passed:
		_failures += 1
	print("%s %s" % ["ok  " if passed else "FAIL", what])
