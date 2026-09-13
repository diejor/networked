extends Node

const GAME := preload("res://examples/rocket_league/main.tscn")


func _ready() -> void:
	var game := GAME.instantiate()
	add_child(game)
	run_regime.call_deferred(game)


func run_regime(game: Node) -> void:
	var api: NetwMultiplayer = game.multiplayer
	await RocketLeagueRegime.attach(game, api).run()
