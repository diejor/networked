## UI metadata describing the address string a [BackendPeer] expects.
##
## Returned by [method BackendPeer.get_address_hint] so generic connect
## dialogs (e.g. [code]ConnectToServerUI[/code]) can render appropriate
## labels, placeholders, and validation cues without knowing about each
## backend.
@tool
class_name AddressHint
extends RefCounted


## Human-readable label for the address field, e.g. [code]"Server IP"[/code],
## [code]"Room Hash"[/code], [code]"Session ID"[/code].
var label: String = "Address"

## Placeholder shown in an empty field, e.g. [code]"localhost"[/code],
## [code]"20-char hex"[/code].
var placeholder: String = ""

## Longer description of what the user should type, surfaced as tooltip
## or help text.
var help_text: String = ""

## Optional regular expression the address must match. Empty string means
## any input is accepted.
var validator_regex: String = ""

## If [code]true[/code], the backend accepts an empty address string
## (typically meaning "use default / public host").
var accepts_empty: bool = false

## If [code]true[/code], the backend implements [method BackendPeer.probe]
## with a meaningful answer. UIs can hide a probe button otherwise.
var supports_probe: bool = false

## If [code]true[/code], this backend has no notion of an external address
## (e.g. in-process loopback). UIs can hide the address field entirely.
var hides_address_field: bool = false


static func make(
	p_label: String,
	p_placeholder: String = "",
	p_help: String = "",
	p_accepts_empty: bool = false,
	p_supports_probe: bool = false
) -> AddressHint:
	var h := AddressHint.new()
	h.label = p_label
	h.placeholder = p_placeholder
	h.help_text = p_help
	h.accepts_empty = p_accepts_empty
	h.supports_probe = p_supports_probe
	return h
