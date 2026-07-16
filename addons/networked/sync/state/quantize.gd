@tool
## Lossy per-value encoder assignable to a synchronizer property.
##
## A quantizer turns a value into a fixed number of bits and back, trading
## precision for size. It is the only "schema" object in the codec stack.
## There is no separate schema container. Assignment is per property through
## [method NetwScriptModel.SyncConfig.quantize]. The same resource can be shared
## across properties by reference. A quantizer is type-aware, so one instance
## handles [Vector2], [float], or [int] without nesting.
##
## [codeblock]
## # Assigned on a synchronizer's codec/<prop> slot, or in code:
## Netw.configure_property(self, &"position").quantize(NetwQuantizeFixed.new())
## [/codeblock]
##
## Widths come from this resource on both peers, never the wire, so the decoder
## reconstructs the exact layout the encoder wrote. This base is abstract: a
## subclass ([NetwQuantizeFixed], [NetwQuantizeBits], [NetwQuantizeAngle],
## [NetwQuantizeQuaternion], [NetwQuantizeTransform2D],
## [NetwQuantizeTransform3D]) supplies the actual layout.
@abstract
class_name NetwQuantize
extends Resource

## Writes [param value] into [param w] using this quantizer's layout.
##
## A subclass encodes by type: a [Vector2] writes each axis, a scalar writes
## one.
@abstract func _write(w: NetwBitBuffer.Writer, value: Variant) -> void


## Reads a value of [param type] back from [param r], inverting [method _write].
@abstract func _read(r: NetwBitBuffer.Reader, type: Variant.Type) -> Variant


## Returns whether this quantizer can encode a value of [param type].
##
## Callers that quantize opportunistically (entity RPC arguments) ask this
## before handing a value to [method _write], so the set of encodable types
## stays owned by each quantizer instead of duplicated at the call site.
@abstract func _supports_type(type: Variant.Type) -> bool


## Returns the bit count this quantizer writes for a value of [param type].
@abstract func _bit_width(type: Variant.Type) -> int


## Returns the worst-case round-trip error for a value of [param type].
##
## For a [Vector2] this is the error magnitude with both axes at their bound,
## so it compares directly against a reconciliation deadzone
## ([member PredictionComponent.divergence_epsilon]). A correction threshold
## below this value triggers on quantization noise alone.
@abstract func _max_error(type: Variant.Type) -> float


## Returns whether [param other] encodes the identical bit layout: the same
## quantizer script with the same exported parameters.
##
## Two layout-equal quantizers read each other's bits, so a configuration
## re-declared per instance with fresh but identical quantizers is the same
## schema, not a conflict. [method NetwScriptModel.SyncConfig.quantize] warns
## only when a re-declaration fails this check.
func is_same_layout(other: NetwQuantize) -> bool:
	if other == self:
		return true
	if other == null or other.get_script() != get_script():
		return false
	for prop in get_property_list():
		if prop["usage"] & PROPERTY_USAGE_SCRIPT_VARIABLE == 0:
			continue
		var prop_name: StringName = prop["name"]
		if get(prop_name) != other.get(prop_name):
			return false
	return true
