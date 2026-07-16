## UI hint for the address a [NetwTransport] accepts.
##
## [method NetwTransport._address_hint] returns this so a connect dialog can
## label its address field, validate input, and decide whether a probe button is
## useful, all before any peer exists. It is authoring metadata only and drives
## no connection behavior.
## [codeblock]
## var hint := NetwAddressHint.make("Room code", "abc123", "", true, false)
## [/codeblock]
class_name NetwAddressHint
extends RefCounted

## Label for the address field.
var label: String = "Address"

## Placeholder shown in an empty field.
var placeholder: String = ""

## Help text for tooltips or inline hints.
var help_text: String = ""

## Optional regular expression validating the field.
var validator_regex: String = ""

## [code]true[/code] when an empty address is valid.
var accepts_empty: bool = false

## [code]true[/code] when a probe of this address is useful.
var supports_probe: bool = false

## [code]true[/code] when the address field should be hidden.
var hides_address_field: bool = false


## Creates a [NetwAddressHint] from the common UI fields.
static func make(
		p_label: String,
		p_placeholder: String = "",
		p_help: String = "",
		p_accepts_empty: bool = false,
		p_supports_probe: bool = false,
) -> NetwAddressHint:
	var h := NetwAddressHint.new()
	h.label = p_label
	h.placeholder = p_placeholder
	h.help_text = p_help
	h.accepts_empty = p_accepts_empty
	h.supports_probe = p_supports_probe
	return h
