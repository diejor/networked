extends Node

const GAME := preload("res://examples/racing/main.tscn")


func _ready() -> void:
	var game := GAME.instantiate()
	add_child(game)
	run_regime.call_deferred(game)


func run_regime(game: Node) -> void:
	var api: NetwMultiplayer = game.multiplayer
	await RacingRegime.attach(game, api).run()
