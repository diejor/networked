@tool
## Lossy per-value encoder assignable to a synchronizer property.
##
## A quantizer turns a value into a fixed number of bits and back, trading
## precision for size. It is the only "schema" object in the codec stack: there is
## no separate schema container. Assignment is per property on the synchronizer
## (see [StampedSynchronizer]); the same resource can be shared across properties
## by reference. A quantizer is type-aware, so one instance handles [Vector2],
## [float], or [int] without nesting.
##
## [codeblock]
## # Assigned on a synchronizer's codec/<prop> slot, or in code:
## register_property(&"position", path).quantize(NetwQuantizeFixed.new())
## [/codeblock]
##
## Widths come from this resource on both peers, never the wire, so the decoder
## reconstructs the exact layout the encoder wrote. This base is abstract: a
## subclass ([NetwQuantizeFixed], [NetwQuantizeBits], [NetwQuantizeAngle]) supplies
## the actual layout.
@abstract
class_name NetwQuantize
extends Resource


## Writes [param value] into [param w] using this quantizer's layout.
##
## A subclass encodes by type: a [Vector2] writes each axis, a scalar writes one.
@abstract func write(w: NetwBitBuffer.Writer, value: Variant) -> void


## Reads a value of [param type] back from [param r], inverting [method write].
@abstract func read(r: NetwBitBuffer.Reader, type: Variant.Type) -> Variant


## Returns the bit count this quantizer writes for a value of [param type].
@abstract func bit_width(type: Variant.Type) -> int


## Returns the worst-case round-trip error for a value of [param type].
##
## For a [Vector2] this is the error magnitude with both axes at their bound, so it
## compares directly against a reconciliation deadzone
## ([member PredictionComponent.divergence_epsilon]). A correction threshold below
## this value triggers on quantization noise alone.
@abstract func max_error(type: Variant.Type) -> float
