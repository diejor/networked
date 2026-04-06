class_name PeerContext
extends RefCounted

## A generic per-peer storage container tied to a [MultiplayerTree] session.
##
## Components register their own typed [Bucket] inner classes here rather than
## using static variables. Each bucket type is keyed by its inner class object,
## so [PeerContext] never imports or references consumer types.
##
## Lifecycle: when [MultiplayerTree] erases a peer's context on disconnect, all
## buckets are freed automatically via [RefCounted] reference counting.

var _buckets: Dictionary = {}


## Returns the bucket for [param bucket_type], creating it on first access.
## [param bucket_type] must be an inner class that extends [RefCounted].
func get_bucket(bucket_type) -> RefCounted:
	if bucket_type not in _buckets:
		_buckets[bucket_type] = bucket_type.new()
	return _buckets[bucket_type]
