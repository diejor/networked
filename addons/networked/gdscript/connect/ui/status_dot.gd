## A coloured dot showing a [member NetwEndpoint.status].
class_name StatusDot
extends Panel

const WORDS := {
	NetwEndpoint.STATUS_UNKNOWN: "Checking",
	NetwEndpoint.STATUS_ONLINE: "Online",
	NetwEndpoint.STATUS_FULL: "Full",
	NetwEndpoint.STATUS_UNREACHABLE: "Unreachable",
	NetwEndpoint.STATUS_INCOMPATIBLE: "Incompatible",
	NetwEndpoint.STATUS_UNSUPPORTED: "Unsupported",
}
const COLORS := {
	NetwEndpoint.STATUS_UNKNOWN: Color(0.6, 0.6, 0.6),
	NetwEndpoint.STATUS_ONLINE: Color(0.24, 0.81, 0.44),
	NetwEndpoint.STATUS_FULL: Color(0.95, 0.77, 0.06),
	NetwEndpoint.STATUS_UNREACHABLE: Color(0.91, 0.3, 0.24),
	NetwEndpoint.STATUS_INCOMPATIBLE: Color(0.6, 0.35, 0.85),
	NetwEndpoint.STATUS_UNSUPPORTED: Color(0.45, 0.45, 0.5),
}

var status: NetwEndpoint.Status = NetwEndpoint.STATUS_UNKNOWN:
	set(value):
		status = value
		self_modulate = COLORS[value]
		tooltip_text = WORDS[value]
