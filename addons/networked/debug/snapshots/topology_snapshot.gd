## Debug event carrying the full synchronizer topology for one player node.
##
## Sent by [NetworkedDebugReporter] on every player spawn.
## [br][br]
## The topology panel treats the latest entry as ground truth - it always shows
## the most recent snapshot, not a time-series.
class_name NetTopologySnapshot
extends RefCounted


## Per-property metadata within a synchronizer.
class PropInfo:
	## Full [NodePath] string, e.g. [code]":position"[/code].
	var path: String

	## [enum Variant.Type] of the property.
	var type: int

	## Class name of the owner node or resource.
	var target_class: String

	## [enum SceneReplicationConfig.ReplicationMode] of the property.
	var replication_mode: int

	## [code]true[/code] if the property is included in the spawn packet.
	var spawn: bool

	## [code]true[/code] if the property is included in delta sync packets.
	var sync: bool

	## [code]true[/code] if it only replicates when the value changes.
	var watch: bool

	## Mirror or virtual source path. Empty if it owns the data.
	var source_path: String

	func to_dict() -> Dictionary:
		return {
			"path": path,
			"type": type,
			"target_class": target_class,
			"replication_mode": replication_mode,
			"spawn": spawn,
			"sync": sync,
			"watch": watch,
			"source_path": source_path,
		}

	static func from_dict(d: Dictionary) -> PropInfo:
		var p := PropInfo.new()
		p.path = d.get("path", "")
		p.type = d.get("type", 0)
		p.target_class = d.get("target_class", "")
		p.replication_mode = d.get("replication_mode", 0)
		p.spawn = d.get("spawn", false)
		p.sync = d.get("sync", false)
		p.watch = d.get("watch", false)
		p.source_path = d.get("source_path", "")
		return p


## Per-synchronizer metadata for one player node.
class SyncInfo:
	## Synchronizer node name.
	var name: String

	## [member MultiplayerSynchronizer.root_path] as a string.
	var root_path: String

	## Peer ID that owns the synchronizer.
	var authority: int

	## Whether the synchronizer is currently active.
	var enabled: bool

	## Array of [NetTopologySnapshot.PropInfo].
	var properties: Array

	func to_dict() -> Dictionary:
		var props: Array = []
		for p: PropInfo in properties:
			props.append(p.to_dict())
		return {
			"name": name,
			"root_path": root_path,
			"authority": authority,
			"enabled": enabled,
			"properties": props,
		}

	static func from_dict(d: Dictionary) -> SyncInfo:
		var s := SyncInfo.new()
		s.name = d.get("name", "")
		s.root_path = d.get("root_path", "")
		s.authority = d.get("authority", 0)
		s.enabled = d.get("enabled", true)
		for pd: Dictionary in d.get("properties", []):
			s.properties.append(PropInfo.from_dict(pd))
		return s


var tree_name: String = ""
var node_path: String = ""
var username: String = ""
var peer_id: int = 0
var lobby_name: String = ""
var active_scene: String = ""
var is_server: bool = false


## Cache diagnostic data: [code]{"hit": bool, "hooked": bool}[/code].
var cache_info: Dictionary = {}

## Array of [NetTopologySnapshot.SyncInfo].
var synchronizers: Array = []


## Serializes this snapshot into a [Dictionary].
func to_dict() -> Dictionary:
	var syncs: Array = []
	for s: SyncInfo in synchronizers:
		syncs.append(s.to_dict())
	return {
		"tree_name": tree_name,
		"node_path": node_path,
		"username": username,
		"peer_id": peer_id,
		"lobby_name": lobby_name,
		"active_scene": active_scene,
		"is_server": is_server,
		"cache_info": cache_info,
		"synchronizers": syncs,
	}


## Creates a [NetTopologySnapshot] from a [Dictionary].
static func from_dict(d: Dictionary) -> NetTopologySnapshot:
	var snap := NetTopologySnapshot.new()
	snap.tree_name = d.get("tree_name", "")
	snap.node_path = d.get("node_path", "")
	snap.username = d.get("username", "")
	snap.peer_id = d.get("peer_id", 0)
	snap.lobby_name = d.get("lobby_name", "")
	snap.active_scene = d.get("active_scene", "")
	snap.is_server = d.get("is_server", false)
	snap.cache_info = d.get("cache_info", {})
	for sd: Dictionary in d.get("synchronizers", []):
		snap.synchronizers.append(SyncInfo.from_dict(sd))
	return snap

