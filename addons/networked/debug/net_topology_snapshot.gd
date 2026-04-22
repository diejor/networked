## Debug event datatype carrying the full synchronizer topology for one player node.
##
## Sent by [NetworkedDebugReporter] on every player spawn via
## [code]emit_debug_event("networked:topology_snapshot", snap.to_dict())[/code].
## The topology panel treats the latest entry as ground truth — it always shows the
## most recent snapshot, not a time-series.
class_name NetTopologySnapshot
extends RefCounted


## Per-property metadata within a synchronizer.
class PropInfo:
	var path: String          ## Full NodePath string, e.g. [code]":position"[/code]
	var type: int             ## Variant.Type of the property
	var target_class: String  ## Class name of the owner node/resource
	var replication_mode: int ## [constant SceneReplicationConfig.REPLICATION_MODE_ALWAYS] etc.
	var spawn: bool           ## True = property is included in the spawn packet
	var sync: bool            ## True = property is included in delta sync packets
	var watch: bool           ## True = only replicates when the value changes
	var source_path: String   ## Empty = owns data; non-empty = mirror/virtual source path

	func to_dict() -> Dictionary:
		return {
			path = path,
			type = type,
			target_class = target_class,
			replication_mode = replication_mode,
			spawn = spawn,
			sync = sync,
			watch = watch,
			source_path = source_path,
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
	var name: String        ## Synchronizer node name
	var root_path: String   ## [member MultiplayerSynchronizer.root_path] as string
	var authority: int      ## Peer ID that owns the synchronizer
	var enabled: bool       ## Whether the synchronizer is currently active
	var properties: Array   ## Array[PropInfo]

	func to_dict() -> Dictionary:
		var props: Array = []
		for p: PropInfo in properties:
			props.append(p.to_dict())
		return {
			name = name,
			root_path = root_path,
			authority = authority,
			enabled = enabled,
			properties = props,
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
var is_server: bool = false
var cache_info: Dictionary = {}  ## Cache diagnostic data: {hit: bool, hooked: bool}
var synchronizers: Array = []  ## Array[SyncInfo]


func to_dict() -> Dictionary:
	var syncs: Array = []
	for s: SyncInfo in synchronizers:
		syncs.append(s.to_dict())
	return {
		tree_name = tree_name,
		node_path = node_path,
		username = username,
		peer_id = peer_id,
		lobby_name = lobby_name,
		is_server = is_server,
		cache_info = cache_info,
		synchronizers = syncs,
	}


static func from_dict(d: Dictionary) -> NetTopologySnapshot:
	var snap := NetTopologySnapshot.new()
	snap.tree_name = d.get("tree_name", "")
	snap.node_path = d.get("node_path", "")
	snap.username = d.get("username", "")
	snap.peer_id = d.get("peer_id", 0)
	snap.lobby_name = d.get("lobby_name", "")
	snap.is_server = d.get("is_server", false)
	snap.cache_info = d.get("cache_info", {})
	for sd: Dictionary in d.get("synchronizers", []):
		snap.synchronizers.append(SyncInfo.from_dict(sd))
	return snap
