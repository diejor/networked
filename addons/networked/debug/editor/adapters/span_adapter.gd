## Adapter that buffers span lifecycle events for one peer.
##
## [member ring_buffer] entries are [code]{"type": String, "data": Dictionary}[/code]
## where [code]type[/code] is one of [code]"open"[/code], [code]"step"[/code],
## [code]"close"[/code], or [code]"fail"[/code], matching the suffix of the
## [code]networked:span_*[/code] message that produced them.
##
## Use [method feed_span] instead of the base [method PanelDataAdapter.feed].
@tool
class_name SpanAdapter
extends PanelDataAdapter


func _init(p_tree_name: String) -> void:
	tree_name = p_tree_name
	panel_type = PanelType.SPAN
	adapter_key = "%s:%s" % [tree_name, PANEL_NAMES[PanelType.SPAN]]


## Appends a span event. [param msg_type] is the suffix after [code]networked:span_[/code]:
## [code]"open"[/code], [code]"step"[/code], [code]"close"[/code], or [code]"fail"[/code].
func feed_span(d: Dictionary, msg_type: String) -> void:
	_push({"type": msg_type, "data": d})


## Returns the count of open span entries still in the buffer.
func get_current_label() -> String:
	var open_count: int = 0
	for e: Dictionary in ring_buffer:
		if (e as Dictionary).get("type", "") == "open":
			open_count += 1
	return "%d spans" % open_count
