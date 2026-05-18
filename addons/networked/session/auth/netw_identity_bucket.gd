## Per-peer storage bucket holding a validated [NetwIdentity].
##
## Retrieved via [method NetwPeerContext.get_bucket]:
## [codeblock]
## var bucket = peer_context.get_bucket(NetwIdentityBucket)
## var identity = bucket.identity
## [/codeblock]
class_name NetwIdentityBucket
extends RefCounted

## The validated identity for this peer, or [code]null[/code] if
## authentication has not completed.
var identity: NetwIdentity
