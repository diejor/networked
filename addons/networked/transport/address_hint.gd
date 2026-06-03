## UI hint for the address accepted by a [BackendPeer].
##
## [method BackendPeer.get_address_hint] returns this for generic connect
## dialogs.
@tool
class_name AddressHint
extends RefCounted

## Label for the address field.
var label: String = "Address"

## Placeholder shown in an empty field.
var placeholder: String = ""

## Help text for tooltips or inline hints.
var help_text: String = ""

## Optional regular expression for validation.
var validator_regex: String = ""

## [code]true[/code] when an empty address is valid.
var accepts_empty: bool = false

## [code]true[/code] when [method BackendPeer.query_server_info] is useful.
var supports_probe: bool = false

## [code]true[/code] when address input should be hidden.
var hides_address_field: bool = false


## Creates an [AddressHint] from the common UI fields.
static func make(
		p_label: String,
		p_placeholder: String = "",
		p_help: String = "",
		p_accepts_empty: bool = false,
		p_supports_probe: bool = false,
) -> AddressHint:
	var h := AddressHint.new()
	h.label = p_label
	h.placeholder = p_placeholder
	h.help_text = p_help
	h.accepts_empty = p_accepts_empty
	h.supports_probe = p_supports_probe
	return h
