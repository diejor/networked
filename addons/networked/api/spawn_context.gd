## Orchestrates the server-side spawn sequence for a single connecting peer.
##
## Construct one per join request in [method SpawnerComponent._on_player_joined]
## and call [method spawn_player] to run the full sequence. Three signals
## expose phase hooks without requiring subclassing.
## [br][br]
## For an ergonomic [member SpawnerComponent.spawn_function] implementation,
## use [method make_spawn_function].
class_name SpawnContext
extends RefCounted

## Emitted after the player node is instantiated but before tree insertion.
signal player_instantiated(player: Node)
## Emitted after [SaveComponent.spawn] completes, or skipped when absent.
signal state_loaded(player: Node)
## Emitted after the player is placed in the scene tree.
signal player_placed(player: Node)

var spawner: SpawnerComponent
var _slot: MultiplayerTree.SpawnSlot
var _context: NetwContext
var _dbg: NetwHandle


## Constructs a [SpawnContext] for a single join request.
##
## [param p_spawner] is the [SpawnerComponent] template that owns the player
## scene. [param slot] is the resolved [MultiplayerTree.SpawnSlot].
## [param context] provides scene and clock access.
func _init(
	p_spawner: SpawnerComponent,
	slot: MultiplayerTree.SpawnSlot,
	context: NetwContext,
) -> void:
	spawner = p_spawner
	_slot = slot
	_context = context
	_dbg = Netw.dbg.handle(spawner)


## Runs the full player spawn sequence.
##
## Preconditions - logs [method push_error] and returns on violation:
## [br]- Must be called on the server.
## [br]- [param client_data] must have a non-zero
##   [member MultiplayerClientData.peer_id], a non-empty
##   [member MultiplayerClientData.username], and a valid
##   [member MultiplayerClientData.spawner_path].
## [br][br]
## Emits [signal player_instantiated], [signal state_loaded], and
## [signal player_placed] at each phase boundary.
func spawn_player(client_data: MultiplayerClientData) -> void:
	if not _context.tree.is_server():
		push_error(
			"SpawnContext.spawn_player must only be called on the server."
		)
		return

	if not client_data.peer_id or not client_data.spawner_path \
			or client_data.username.is_empty():
		_dbg.error(
			"Player join failed: invalid client data.",
			[], func(m): push_error(m)
		)
		return

	var span := _dbg.span("player_join", {
		"username": client_data.username,
		"peer_id": client_data.peer_id,
		"authority_mode": spawner.authority_mode,
	})
	_dbg.info(
		"Player joined: %s (ID: %d)",
		[client_data.username, client_data.peer_id]
	)
	span.step("joined")

	var player := _instantiate_player(client_data)
	player_instantiated.emit(player)

	var save: SaveComponent = player.get_node_or_null("%SaveComponent")
	if save:
		save.spawn(spawner.owner, span)
	state_loaded.emit(player)

	var tp: TPComponent = player.get_node_or_null("%TPComponent")
	var lobby_mgr := _context.tree.get_scene_manager()

	if tp and save and lobby_mgr:
		tp.spawn(lobby_mgr)
	else:
		_slot.place_player(player)
	player_placed.emit(player)

	span.end()


## Creates a player node from the spawner owner scene.
##
## Sets the node [code]name[/code] to [code]"username|peer_id"[/code] and
## assigns [member SpawnerComponent.username].
func _instantiate_player(client_data: MultiplayerClientData) -> Node:
	_dbg.trace(
		"Instantiating player for %s (ID: %d)",
		[client_data.username, client_data.peer_id]
	)
	var player: Node = load(spawner.owner.scene_file_path).instantiate()
	var comp: SpawnerComponent = player.get_node("%SpawnerComponent")
	comp.username = client_data.username
	player.name = "%s|%s" % [client_data.username, client_data.peer_id]
	return player


## Returns a [Callable] for [member SpawnerComponent.spawn_function].
##
## The callable signature is
## [code]func(ctx: SpawnContext, data: MultiplayerClientData)[/code].
## When [param hook] is provided, it runs first so the caller can adjust
## [member SpawnerComponent.authority_mode] before [method spawn_player] runs.
## [br][br]
## [b]Example:[/b]
## [codeblock]
## var am := SpawnerComponent.AuthorityMode
## spawner.spawn_function = SpawnContext.make_spawn_function(
##     func(ctx, _data):
##         ctx._spawner.authority_mode = am.SERVER_AUTHORITATIVE
## )
## [/codeblock]
static func make_spawn_function(hook: Callable = Callable()) -> Callable:
	return func(ctx: SpawnContext, data: MultiplayerClientData) -> void:
		if hook.is_valid():
			hook.call(ctx, data)
		ctx.spawn_player(data)
