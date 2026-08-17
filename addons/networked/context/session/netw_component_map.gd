## The component-ID map for one [NetwEntity].
##
## Pairs each registered sub-node with a stable 1-byte id, so an entity RPC or a
## masked sync frame addresses a component by id instead of a [NodePath]. The
## map's [member table_hash] rides the spawn packet. When a client computes a
## different hash than the [member wire_hash] the server authored, the two
## structures disagree, so the map is [member poisoned] and every routed
## frame falls back to string paths and names rather than misrouting.
## [codeblock]
## entity.register_component(gun)
## var id := entity.components.id_for_path(NodePath("Gun"))   # 1-byte handle
## var path := entity.components.path_for_id(id)              # reverse lookup
## [/codeblock]
class_name NetwComponentMap
extends RefCounted


## [code]true[/code] once a client hash mismatch was detected, so routed
## frames fall back to string paths and names instead of ids.
var poisoned: bool:
	get:
		return _table.poisoned

## The 16-bit hash of this peer's own component structure, computed in
## [method hydrate].
var table_hash: int:
	get:
		return _table.table_hash

## The 16-bit hash the server authored, carried on the spawn packet. A
## client compares it against [member table_hash] in [method reconcile].
var wire_hash: int:
	get:
		return _table.wire_hash
	set(value):
		_table.wire_hash = value

# The id plane: the sorted paths, the ids they carry, and what an address off
# the wire is allowed to name.
var _table := NetwCompTable.new()

var _entity_ref: WeakRef
var _registered: Array[Node] = []


func _init(entity: NetwEntity) -> void:
	_entity_ref = weakref(entity)


## Returns the [NodePath] registered under [param id], or an empty path when
## [param id] is unknown.
func path_for_id(id: int) -> NodePath:
	return NodePath(_table.path_for_id(id))


## Returns the 1-byte id registered for [param path], or [code]0[/code] when
## the path is absent. Since ids start at [code]1[/code], guard a lookup with
## [method has_path] when [code]0[/code] must be told apart from a miss.
func id_for_path(path: NodePath) -> int:
	return _table.id_for_path(String(path))


## [code]true[/code] when [param path] has a registered id.
func has_path(path: NodePath) -> bool:
	return _table.has_path(String(path))


## Returns what the address [param comp] and [param path] names, as one of
## [enum NetwCompTable.Address], before any node is looked up.
##
## An address arrives off the wire, so the answer is a refusal wherever this
## map cannot name a node: a fallback path shaped to leave the entity subtree
## is [constant NetwCompTable.ADDRESS_HOSTILE] and is never resolved, and an id
## this table does not carry, including every id once it is [member poisoned],
## is [constant NetwCompTable.ADDRESS_UNMAPPED].
## [codeblock]
## match entity.components.classify(comp, path):
##     NetwCompTable.ADDRESS_ROOT:      entity.owner
##     NetwCompTable.ADDRESS_MAPPED:    entity.components.path_for_id(comp)
##     NetwCompTable.ADDRESS_RELATIVE:  path, once proven a descendant
## [/codeblock]
func classify(comp: int, path: String) -> int:
	return _table.classify(comp, path)


## Returns the node [param comp] and [param path] address under [param owner],
## or [code]null[/code].
##
## [method classify] answered without looking at the tree, so it can only leave
## the descendant proof owed. This is [method classify] with that proof
## discharged, which is why an address arriving off the wire is resolved here
## rather than by each caller repeating the containment check.
func resolve_node(owner: Node, comp: int, path: String) -> Node:
	return _table.resolve_node(owner, comp, path)


## Registers [param component] as an addressable sub-node. A registration
## after [method hydrate] has sealed the ids warns, since it arrives too late
## to ride the spawn packet.
func register(component: Node) -> void:
	var e := _entity_ref.get_ref() as NetwEntity
	if e == null or component == e.owner:
		return
	if _registered.has(component):
		return
	_registered.append(component)
	if table_hash != 0:
		Netw.dbg.warn(
			"register_component: Node '%s' registered after component table "
			+ "hydration. Configure networked properties inside _init() to "
			+ "prevent this.",
			[component.name],
			func(m): push_warning(m),
		)


## Builds the id mapping from the registered components and computes
## [member table_hash].
func hydrate() -> void:
	var e := _entity_ref.get_ref() as NetwEntity
	if e == null:
		return
	var paths := PackedStringArray()
	for comp in _registered:
		if is_instance_valid(comp):
			var rel := e.relative_path(e.owner, comp)
			if not rel.is_empty():
				paths.append(String(rel))
	_table.assign(paths)
	_table.table_hash = _compute_hash(_table.sorted_paths())


## Reconciles the computed hash against the wire. On the server, adopts
## [member table_hash] as [member wire_hash]. On a client, a mismatch with
## the received [member wire_hash] leaves the table [member poisoned].
func reconcile() -> void:
	var e := _entity_ref.get_ref() as NetwEntity
	if e == null:
		return
	if not _table.reconcile(e.is_authority):
		return
	Netw.dbg.warn(
		"Component table hash mismatch on entity '%s': server=%d, "
		+ "client=%d. Table poisoned.",
		[e.entity_id, wire_hash, table_hash],
	)


# The 16-bit hash covers the component paths (which drive comp ids) and every
# routed script's sorted @rpc method, script property, and signal name lists
# (which drive 1-byte method, property, and signal ids), so a version skew in
# any of them falls back to string paths and names instead of misrouting.
func _compute_hash(paths: PackedStringArray) -> int:
	var e := _entity_ref.get_ref() as NetwEntity
	var s := ""
	for path in paths:
		s += str(path) + ","

	s += "|methods:"
	var scripts: Array[Script] = []
	if e and is_instance_valid(e.owner) and e.owner.get_script():
		scripts.append(e.owner.get_script())
	for comp in _registered:
		if is_instance_valid(comp) and comp.get_script():
			scripts.append(comp.get_script())
	for sc in scripts:
		var methods: Array = []
		var cfg: Dictionary = sc.get_rpc_config()
		if cfg:
			for m in cfg:
				methods.append(String(m))
		methods.sort()
		s += "|m:" + "/".join(methods)

		# Property and signal name sets drive 1-byte property/signal ids, so
		# a skew in either must poison the table exactly like a method skew.
		var props: Array = []
		var sigs: Array = []
		var base := sc
		while base != null:
			for p in base.get_script_property_list():
				if int(p.get("usage", 0)) & PROPERTY_USAGE_SCRIPT_VARIABLE:
					var pn := String(p["name"])
					if not props.has(pn):
						props.append(pn)
			for sig in base.get_script_signal_list():
				var sn := String(sig["name"])
				if not sigs.has(sn):
					sigs.append(sn)
			base = base.get_base_script()
		props.sort()
		sigs.sort()
		s += "|p:" + "/".join(props)
		s += "|s:" + "/".join(sigs)

	return s.hash() & 0xFFFF
