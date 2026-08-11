## Writer-seam backend that captures the displayed sequence instead of a node.
##
## The interpolation engine emits every smoothed value through its writer seam.
## The node writer sets a property on a live [Node]; this backend appends the
## value to a log with the frame time that produced it, so the display calculus
## can assert signal properties on the output with no [SceneTree] in reach. It is
## the concrete first payoff of the seam being swappable, and it declares the
## [code]ANY[/code] thread class every non-node writer carries.
## [codeblock]
## var writer := NetwInterpRecordingWriter.new()
## state.output = writer                 # replaces the node writer
## writer.mark_frame(wall_seconds)       # stamps the next writes
## # ... pump runs, calling writer.write(value) ...
## var last := writer.samples.back()     # the captured displayed value
## [/codeblock]
class_name NetwInterpRecordingWriter
extends DisplayCore._Output

## Every value written, in emission order.
var samples: Array = []

## Wall-clock frame time in seconds paired with each entry in [member samples].
var frame_times: PackedFloat64Array = PackedFloat64Array()

var _frame_time := 0.0


## Stamps the frame time carried by every following [method write] until the next
## call. The harness marks the frame before pumping so a captured write carries
## the frame that produced it.
func mark_frame(seconds: float) -> void:
	_frame_time = seconds


## The recording writer runs off any thread because it touches no node.
func thread_class() -> StringName:
	return &"ANY"


## Captures [param value] with the current frame time instead of writing a node.
func write(value: Variant) -> void:
	samples.append(value)
	frame_times.append(_frame_time)


## Drops every captured write so one writer can drive several runs.
func reset() -> void:
	samples.clear()
	frame_times.clear()
	_frame_time = 0.0
