## A generic per-peer storage container tied to a [MultiplayerTree] session.
##
## Components register their own typed [code]Bucket[/code] inner classes here
## rather than using static variables. Each bucket type is keyed by its inner
## class object, so [NetwPeerContext] never imports or references consumer types.
## [br][br]
## [codeblock]
## # Example usage:
## var bucket = peer_context.get_bucket(MyComponent.Bucket)
## bucket.some_state = 123
## [/codeblock]
class_name NetwPeerContext
extends RefCounted

var _buckets: Dictionary = {}


## Returns the bucket for [param bucket_type], creating it on first access.
##
## [param bucket_type] must be a class or inner class that extends [RefCounted].
func get_bucket(bucket_type) -> RefCounted:
	if bucket_type not in _buckets:
		_buckets[bucket_type] = bucket_type.new()
	return _buckets[bucket_type]
