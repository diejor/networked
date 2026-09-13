## One provider's realization of a live session for the embedding-conformance
## suite.
##
## A conformance scenario is written once against this interface and run under
## BOTH providers — [NetwScopedWorld] (a subpath [MultiplayerTree], the harness
## topology) and [NetwRootWorld] (a root-installed session, the shipping
## topology) — so a green scoped run and a green root run assert the SAME
## observable facts. That proven equivalence is what lets the cheap in-process
## harness stand in for the shipping root install. Without it, green means
## "the wrapped world works", never "the shipped game works".
## [codeblock]
## var world := _make_world()          # scoped or root
## var host := await world.host()      # host online through the same verb
## var client := await world.add_client("p1")
## assert_that(host.peer_get_participant(client.get_unique_id())).is_not_null()
## await world.dispose()               # restore process state
## [/codeblock]
##
## Subclasses realize each row against their provider. The scenario code never
## names [MultiplayerTree] or the [SceneTree] default session — only
## this interface — so a scenario cannot silently test one topology.
class_name NetwEmbeddingWorld
extends RefCounted

# A minimal packed scene so a replicated probe carries a reconstruction recipe
# (its scene path) that every peer rebuilds from, no per-peer spawn constructor.
const _PROBE_SCENE := preload("res://tests/support/probe.tscn")


## Brings the host online and returns its [NetwMultiplayer]. Idempotent: a second
## call returns the same online host.
func host() -> NetwMultiplayer:
	return null


## Joins a client named [param username] and returns its [NetwMultiplayer] once it
## is admitted (its [member NetwMultiplayer.local_participant] is set).
func add_client(_username: String) -> NetwMultiplayer:
	return null


func declare_initial_scene(_scene: PackedScene) -> void:
	pass


## Declares a clock on this world's host anchor and settles it, so a scenario
## can assert the configuration lifecycle reaches the session identically under
## both embeddings. [method Netw.configure_clock] resolves the branch
## [NetwMultiplayer] from the scope node, so this must configure the host's
## [method NetwMultiplayer.clock_is_configured] under a root install with no
## owning tree exactly as it does under a scoped tree.
func mount_clock() -> void:
	pass


## Pumps every mount in this world until [param cond] returns [code]true[/code] or
## [param timeout_ms] elapses. Returns the final value of [param cond].
func pump_until(_cond: Callable, _timeout_ms: int = 3000) -> bool:
	return false


## A short human label naming the provider, stamped into assertion messages so a
## conformance failure names the topology that broke.
func provider() -> String:
	return "base"


## Replicates a probe [Node] named [param name] on the host and returns its
## [member NetwEntity.route], so a scenario can assert every admitted peer
## receives it. Concrete because it needs only the provider-agnostic
## [method host] anchor and [method NetwMultiplayer.replicate]: replicate first
## (stamp and arm), then place under [member NetwMultiplayer.root], the one anchor
## that exists on every peer.
##
## [br][br][b]Server Only.[/b]
func spawn_probe(name: String) -> int:
	var host_api := await host()
	var node := _PROBE_SCENE.instantiate()
	node.name = name
	host_api.entity_replicate(node)
	host_api.root.add_child(node)
	return NetwEntity.of(node).route


## Restores process state (peers closed, root override reverted) so the next case
## starts clean. Safe to call more than once.
func dispose() -> void:
	pass
