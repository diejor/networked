## Abstract base for resources that round-trip through a [PackedByteArray].
##
## [Serde] keeps payload types responsible for their own byte representation.
## Callers can pass the result of [method serialize] across the wire or store it
## for a later [method deserialize].
##
## [codeblock]
## var bytes := payload.serialize()
## copy.deserialize(bytes)
## [/codeblock]
@abstract
class_name Serde
extends Resource

## Converts this resource to a [PackedByteArray].
@abstract func serialize() -> PackedByteArray


## Repopulates this resource from [param bytes].
@abstract func deserialize(bytes: PackedByteArray) -> void
