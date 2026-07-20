## Plain counters describing one [InterestEngine] recompute.
class_name InterestStats
extends RefCounted

## Layer masks recomputed by the last pass.
var layers_recomputed: int = 0
## Entity rows recomputed by the last pass.
var entities_recomputed: int = 0
## Admitted entity and peer pairs in the resulting matrix.
var edges: int = 0
## Show transitions in the last delta.
var shows: int = 0
## Hide transitions in the last delta.
var hides: int = 0
## Packed words used by each row.
var words_per_row: int = 0


## Returns the counters as an ordered value array for parity tests.
func to_array() -> Array[int]:
	return [
		layers_recomputed,
		entities_recomputed,
		edges,
		shows,
		hides,
		words_per_row,
	]
