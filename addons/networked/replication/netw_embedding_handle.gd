## The install contract between a session and whatever embedded it, reached
## through [member NetwMultiplayer.embedding].
##
## An embedding decides two things a session cannot decide for itself: when
## authoring is complete, and when the session is released. Both are one call
## here, so a [MultiplayerTree] subtree, a root autoload install, and a
## tree-less pump all bring a session up and tear it down through the identical
## sequence instead of through whatever order their own nodes happened to enter
## in.
## [codeblock]
## var embedding := NetwMultiplayer.of(self).embedding
##
## # Bring-up, once, at the first idle frame after install.
## embedding.offer_bare_level(level)   # optional default single scene
## embedding.settle()                  # DECLARING -> SETTLING -> LIVE
##
## # Teardown, once, when the embedder goes away.
## embedding.dispose()
## [/codeblock]
class_name NetwEmbeddingHandle
extends RefCounted

## The bootstrap phase of a session's authoring, advanced once by the installing
## embedding through [method settle].
##
## Authoring registration is only in-contract during [constant Phase.DECLARING].
## Orthogonal to [enum SessionCore.State]: a [constant Phase.LIVE] session is
## still [constant SessionCore.State.OFFLINE] until it hosts or joins.
enum Phase {
	## Nodes and scripts register configs through
	## [method MultiplayerAPI.object_configuration_add]. Nothing acts on them
	## yet.
	DECLARING,
	## The embedding signalled the authored world is loaded; the session
	## resolves the winning declarations as one ordered step.
	SETTLING,
	## Authoring resolved, session bring-up may proceed.
	LIVE,
}

## Emitted when [member phase] advances. The installing embedding is the only
## caller, through [method settle].
signal phase_changed(phase: Phase)

## The current bootstrap [enum Phase]. Read-only; the installing embedding
## advances it once through [method settle]. Pairs with [signal phase_changed].
var phase: Phase:
	get:
		return _core.phase

var _core: EmbeddingCore


func _init(core: EmbeddingCore) -> void:
	_core = core


## Records the [param level] a scoped embedding offers as its default single
## scene, consumed by the next [method settle]. The embedding captures it
## synchronously so a node dropped in afterwards never becomes the candidate.
func offer_bare_level(level: Node) -> void:
	_core.offer_bare_level(level)


## Resolves this session's authored declarations as one ordered step and
## advances [member phase] to [constant Phase.LIVE].
##
## The installing embedding calls it once at the first idle frame after install:
## a [MultiplayerTree] defers it from [method Node._enter_tree] so the call lands
## after the enclosing authoring wave, and the root autoload calls it on a
## one-shot [signal SceneTree.process_frame]. Idempotent, so a call after
## [constant Phase.DECLARING] returns [constant @GlobalScope.OK].
func settle() -> Error:
	return _core.settle()


## Sends and receives one batch of datagrams, then sweeps what arriving traffic
## retires.
##
## A datagram is only sent or received here, so whatever drives this call decides
## how often a peer's simulation can see the network. Engine polling alone drives
## it once per rendered frame, which makes a peer's framerate govern its
## neighbour's input arrival: commands pile up and land in clumps, and a clumped
## arrival reaches a solver body as a transition that spent the wrong amount of
## physics rather than as latency.
##
## So the transport is also serviced once per tick of the session's clock, which
## gives the guarantee this needs: [b]at least once per simulated tick[/b].
## Servicing a peer more often than that is harmless and only lowers latency, so
## the engine poll keeps its call and a session with no clock is unaffected.
## [codeblock]
## before_tick_loop   record the frame that closed
##   on_tick          tick_step
##   after_tick       flush this tick's frames, then poll_transport()
## after_tick_loop    drive and consume, against a queue the tick just filled
## [/codeblock]
## Safe to call again from game code that wants the newest inbound state before
## it runs.
func poll_transport() -> Error:
	return _core.poll_transport()


## Replaces [member NetwMultiplayer.inner] in place, rebinding this same session
## to a new [SceneMultiplayer]. Used by backends that bring their own transport.
## The [NetwMultiplayer] object, its installation on the embedding's branch, and
## every cached [method NetwMultiplayer.of] reference stay valid across the swap.
func adopt_inner(new_inner: SceneMultiplayer) -> void:
	_core.adopt_inner(new_inner)


## Releases the whole graph this session owns, and is the one teardown call an
## embedding makes.
##
## The session and the [RefCounted] objects it owns hold each other through
## signal connections, and a signal connection strong-references its target, so
## nothing in the group is freed until those edges are cut. This cuts them and
## then calls the owned engines' own teardown in turn. Called by
## [MultiplayerTree] when the tree is deleted, and by
## [method NetwMultiplayer.uninstall_default] for a root install. The session is
## unusable afterwards.
## [codeblock]
## api.embedding.dispose()            the embedding's entry point
##   ┠╴scenes.dispose()
##   ┠╴the session machine
##   ┠╴the display pump
##   ┠╴the replication engine
##   ┖╴the RPC engine
## [/codeblock]
func dispose() -> void:
	_core.dispose()


## Returns whether [method dispose] has begun a deliberate teardown.
##
## The session machine reads this so a peer this session closes itself during
## teardown is never mistaken for a spontaneous server crash, which is the only
## drop that ends a session reactively.
func is_disposing() -> bool:
	return _core.is_disposing()
