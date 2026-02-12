@abstract
class_name Serde
extends Resource

@abstract func serialize() -> PackedByteArray
@abstract func deserialize(bytes: PackedByteArray) -> void
