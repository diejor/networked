class_name PersistenceRestartRows
extends RefCounted

const DATABASE := &"saves"
const SLOT := &"campaign"
const HERO := &"hero"
const FOREST := &"forest"
const MOB_IDS := ["wolf", "bear"]
const MOB_HP := [40, 70]
const GOLD := 12

var players: NetwSchema
var mobs: NetwSchema
var hp: int


func _init() -> void:
	players = Netw.configure_schema(&"restart_players")
	players.replicated(false)
	players.i64(&"gold")
	mobs = Netw.configure_schema(&"restart_mobs")
	hp = mobs.i64(&"hp")


static func mount(tree: SceneTree, name: StringName) -> Node:
	var holder := Node.new()
	holder.name = name
	tree.root.add_child(holder)
	tree.set_multiplayer(NetwMultiplayer.new(), holder.get_path())
	return holder


static func open(holder: Node, root: String) -> NetwDatabase:
	var backend := FileSystemDatabase.new()
	backend.root = root
	Netw.configure_database(holder, DATABASE).backend(backend)
	var db := Netw.database(holder, DATABASE)
	var opened: Error = await db.open(SLOT).wait()
	if opened != OK:
		push_error("opening %s answered %s" % [root, error_string(opened)])
		return null
	return db
