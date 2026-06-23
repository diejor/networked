## Tier 1 rendezvous check: connect two local instances through Nakama, no
## Discord.
##
## Builds a [MultiplayerTree] with a [NakamaSession], [NakamaLobbyDirectory], and
## a [DiscordActivityService] whose rendezvous points at a local Docker Nakama
## ([code]localhost:7350[/code]). The fake instance-id seam drives the whole
## host-or-join path. Run two windows with the same instance id and distinct
## device ids: the first hosts a relay match, the second resolves the same id and
## joins it, and both report two connected peers.
## [codeblock]
## # window A
## godot --path . res://addons/networked_activity/tier1_rendezvous_demo.tscn \
##   -- --netw-discord-fake=instance_id=room1;device_id=alice
## # window B
## godot --path . res://addons/networked_activity/tier1_rendezvous_demo.tscn \
##   -- --netw-discord-fake=instance_id=room1;device_id=bob
## [/codeblock]
extends Control

## Fake instance id used when no [code]--netw-discord-fake[/code] flag is passed,
## so the scene runs straight from the editor play button. Both windows must
## share this value to rendezvous into one match.
@export var instance_id: String = "room1"

## Fake device id used when no command-line flag is passed. Each window needs a
## distinct value, so the two instances are distinct Nakama users. Change this in
## the inspector for the second window, or pass the flag from the command line.
@export var device_id: String = "alice"

@export var nakama_host: String = "127.0.0.1"
@export var nakama_port: int = 7350
@export var nakama_use_ssl: bool = false

@onready var _out: RichTextLabel = $Output

var _tree: MultiplayerTree
var _service: DiscordActivityService


func _ready() -> void:
	_build_tree()
	# Let the tree mount its api and the service register.
	await get_tree().process_frame

	if not _service.in_discord():
		_log("[color=red]No fake instance id.[/color] Pass " +
			"--netw-discord-fake=instance_id=room1;device_id=alice")
		return
	_log("instance_id=[b]%s[/b] device_id=[b]%s[/b]"
			% [_service.instance_id(), _service.device_id()])

	_tree.api.peer_connected.connect(func(id: int) -> void:
		_log("[color=green]peer connected:[/color] %d" % id)
		_report())
	_tree.api.peer_disconnected.connect(func(id: int) -> void:
		_log("[color=orange]peer disconnected:[/color] %d" % id))

	await _service.start()

	var payload := JoinPayload.new()
	var who := _service.device_id()
	payload.username = who if not who.is_empty() else "player"

	_log("connecting ...")
	var err := await _service.connect_activity(payload)
	if err != OK:
		_log("[color=red]connect failed: %s[/color]" % error_string(err))
		return
	_report()


func _build_tree() -> void:
	_tree = MultiplayerTree.new()
	_tree.name = &"Tree"

	var session := NakamaSessionService.new()
	session.name = &"NakamaSession"
	session.host = nakama_host
	session.port = nakama_port
	session.use_ssl = nakama_use_ssl
	_tree.add_child(session)

	var dir := NakamaLobbyDirectory.new()
	dir.name = &"NakamaLobbyDirectory"
	dir.host = nakama_host
	dir.port = nakama_port
	dir.use_ssl = nakama_use_ssl
	_tree.add_child(dir)

	var rdv := NakamaDiscordRendezvous.new()
	rdv.host = nakama_host
	rdv.port = nakama_port
	rdv.use_ssl = nakama_use_ssl

	_service = DiscordActivityService.new()
	_service.name = &"DiscordActivity"
	_service.rendezvous = rdv
	# Editor convenience: seed the fake identity so the play button works. A
	# command-line --netw-discord-fake flag still wins inside set_fake_identity.
	if not instance_id.is_empty():
		_service.set_fake_identity(instance_id, device_id)
	_tree.add_child(_service)

	add_child(_tree)


func _report() -> void:
	if _tree.api == null or not _tree.api.has_multiplayer_peer():
		return
	var me := _tree.api.get_unique_id()
	var peers := _tree.api.get_peers()
	var role := "HOST (peer 1)" if me == 1 else "client"
	_log("state: %s, my id=%d (%s), peers=%s, match=%s"
			% [_tree.state, me, role, peers, _hosted_match()])


func _hosted_match() -> String:
	var dir := _tree.get_service(NakamaLobbyDirectory) as NakamaLobbyDirectory
	return dir.get_join_address() if dir else "?"


func _log(line: String) -> void:
	print(line)
	if is_instance_valid(_out):
		_out.append_text(line + "\n")
