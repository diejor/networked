## Abstract base for resources that can round-trip through a [PackedByteArray].
##
## Extend this to create serializable resources (e.g. [MultiplayerClientData], [DictionarySave]).
@abstract
class_name Serde
extends Resource

## Converts this resource to a [PackedByteArray] suitable for network transmission or disk storage.
@abstract func serialize() -> PackedByteArray

## Repopulates this resource from [param bytes] produced by [method serialize].
@abstract func deserialize(bytes: PackedByteArray) -> void
