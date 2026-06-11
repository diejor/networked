class_name BomberAiSuite
extends NetwTestSuite
## Shared base for AI-driven bomber integration tests.
##
## Provides the game harness, player setup, tick helpers, and world
## query utilities that every AI test category needs.

const MAIN := preload("res://examples/bomber/main.tscn")

const PLAYER_NAMES: Array[String] = [
	"valeria",
	"jose",
	"lucia",
	"carlos",
	"elena",
	"diego",
	"sofia",
	"mateo",
]

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()
	game.show_views()


## Adds [param count] players (one host + clients) and begins the match.
func add_players_and_start(count: int) -> Array[NetwSceneRunner]:
	assert(count >= 1 and count <= PLAYER_NAMES.size())
	var runners: Array[NetwSceneRunner] = []
	runners.append(await game.add_host(PLAYER_NAMES[0], false))
	for i in range(1, count):
		runners.append(
			await game.add_client(PLAYER_NAMES[i], false),
		)
	_begin_game(runners[0])

	for r: NetwSceneRunner in runners:
		await r.await_scene(&"World", 2.0)
	return runners


## Creates one [BomberAI] per runner, all sharing the same [param goal].
func make_ais(
		runners: Array[NetwSceneRunner],
		shared_goal: BomberAI.Goal = null,
) -> Array[BomberAI]:
	var ais: Array[BomberAI] = []
	for r: NetwSceneRunner in runners:
		var ai := BomberAI.create(r, r.username)
		if shared_goal:
			ai.goal = shared_goal
		ais.append(ai)
	return ais


## Ticks all AIs for up to [param max_ticks], stopping when [param done]
## returns [code]true[/code] or when [param timeout_s] is exceeded.
## Returns the tick count reached.
func run_until(
		ais: Array[BomberAI],
		max_ticks: int,
		done: Callable = Callable(),
		timeout_s: float = 5.0,
) -> int:
	var tickrate := 30
	if game.host:
		var clock := game.host.tree.get_service(MultiplayerClock) \
				as MultiplayerClock
		if clock:
			tickrate = clock.tickrate

	var max_timeout_ticks := int(timeout_s * tickrate)
	var limit := mini(max_ticks, max_timeout_ticks)

	for tick in limit:
		for ai: BomberAI in ais:
			ai.tick()
		await game.sync_ticks(1)
		if done.is_valid() and done.call():
			return tick
	return limit


func rocks_left(runner: NetwSceneRunner) -> int:
	var world := _get_world(runner)
	if not world or not world.level:
		return 0
	var rocks := world.level.get_node_or_null("Rocks")
	if not rocks:
		return 0
	return rocks.get_child_count()


func winner_visible(runner: NetwSceneRunner) -> bool:
	var world := _get_world(runner)
	if not world or not world.level:
		return false
	var winner := world.level.get_node_or_null("Winner")
	return winner != null and winner.visible


func get_score(runner: NetwSceneRunner, peer_id: int) -> int:
	var world := _get_world(runner)
	if not world or not world.level:
		return 0
	var score := world.level.get_node_or_null("Score")
	if not score:
		return 0
	return score.get_score(peer_id)


func is_stunned(runner: NetwSceneRunner, pname: StringName) -> bool:
	var p := runner.find_player(pname)
	return p != null and p.get("stunned") as bool


func count_bombs(runner: NetwSceneRunner) -> int:
	var world := _get_world(runner)
	if not world or not world.level:
		return 0
	var count := 0
	for child: Node in world.level.get_children():
		if child is Area2D:
			count += 1
	return count


## Restores normal links for every remote runner.
func settle_network(
		runners: Array[NetwSceneRunner],
		ticks: int = 60,
) -> void:
	if runners.is_empty():
		return
	for i in range(1, runners.size()):
		game.degrade(runners[i]).clear()
	var remaining := ticks
	while remaining > 0:
		var batch := mini(20, remaining)
		await game.sync_ticks(batch)
		remaining -= batch


func _begin_game(host: NetwSceneRunner) -> void:
	var gamestate := host.tree.get_service(BomberGamestate) \
			as BomberGamestate
	assert_that(gamestate).is_not_null()
	gamestate.begin_game()


func _get_world(runner: NetwSceneRunner) -> MultiplayerScene:
	if not runner or not runner.tree:
		return null
	var sm := runner.tree.get_service(MultiplayerSceneManager)
	if not sm:
		return null
	return sm.active_scenes.get(&"World") as MultiplayerScene
