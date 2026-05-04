## Typed manifest for [code]CPP_ERROR_LOG_WATCHDOG[/code] events.
class_name NetCppErrorManifest
extends NetManifest

var errors: Array[String]
var error_text: String


## Serializes this manifest into a [Dictionary].
func to_dict() -> Dictionary:
	var d := super.to_dict()
	d["errors"] = errors
	d["error_text"] = error_text
	return d
