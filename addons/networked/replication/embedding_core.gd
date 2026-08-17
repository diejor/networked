## The bootstrap machine an embedding drives to install and release a session,
## owned by [NetwMultiplayer] and reached through [NetwEmbeddingHandle].
##
## The session always has one, constructed with the [NetwMultiplayer] that owns
## it. Bootstrap ordering is owned rather than emergent: every embedding runs the
## identical [constant NetwEmbeddingHandle.Phase.DECLARING] to
## [constant NetwEmbeddingHandle.Phase.LIVE] sequence, so the order authoring
## resolves in never depends on [method Node._enter_tree] traversal.
## [codeblock]
## DECLARING   object_configuration_add lands scene and session configs
##   |         offer_bare_level() captures the default single scene
##   |         settle()
## SETTLING    the winning declarations resolve as one ordered step
## LIVE        authoring resolved, session bring-up may proceed
##             ...
## dispose()   the owned graph is released and the session is spent
## [/codeblock]
class_name EmbeddingCore
extends RefCounted

const Phase := NetwEmbeddingHandle.Phase

var _api_ref: WeakRef

var _phase: Phase = Phase.DECLARING

# The one direct packed level a scoped embedding offers for automatic adoption,
# captured by the embedding before settle. Null under a root install.
var _bare_level_candidate: Node


func _init(api: NetwMultiplayer = null) -> void:
	_api_ref = weakref(api) if api else null


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null

## The current bootstrap phase, read through
## [member NetwEmbeddingHandle.phase].
var phase: Phase:
	get:
		return _phase


## Records the level a scoped embedding offers as its default single scene,
## consumed by the next [method settle]. Reached through
## [method NetwEmbeddingHandle.offer_bare_level].
func offer_bare_level(level: Node) -> void:
	_bare_level_candidate = level


## Resolves the session's authored declarations as one ordered step and advances
## [member phase] to [constant NetwEmbeddingHandle.Phase.LIVE]. Idempotent, so a
## call after [constant NetwEmbeddingHandle.Phase.DECLARING] returns
## [constant @GlobalScope.OK]. Reached through
## [method NetwEmbeddingHandle.settle].
##
## Returns [constant @GlobalScope.ERR_UNCONFIGURED] when a peer-scoped channel
## the wire declares reached settle with no handler registered for it.
func settle() -> Error:
	if _phase != Phase.DECLARING:
		return OK
	_set_phase(Phase.SETTLING)
	_run_settle_resolve()
	var api := _api()
	var wired := api._replication.settle_channels() if api else OK
	_set_phase(Phase.LIVE)
	return wired


func _set_phase(value: Phase) -> void:
	if _phase == value:
		return
	_phase = value
	var api := _api()
	if api:
		api.embedding.phase_changed.emit(_phase)


# The authoring resolution the embedding used to defer piecemeal from
# _enter_tree, now one fixed-order step: adopt the offered bare level (which
# registers the declaration), then ensure the host scene view.
func _run_settle_resolve() -> void:
	var api := _api()
	if api == null:
		return
	api._scenes._adopt_bare_level(_bare_level_candidate)
	_bare_level_candidate = null
	api._scenes._ensure_host_scene_view()


## Sends and receives one batch of datagrams, then sweeps what arriving traffic
## retires. Called once per rendered frame and once per simulation tick, which
## is what makes [b]at least once per simulated tick[/b] hold. Reached through
## [method NetwEmbeddingHandle.poll_transport].
func poll_transport() -> Error:
	var api := _api()
	if api == null:
		return ERR_UNCONFIGURED
	var err := api.inner.poll()
	api._liveness_poll()
	api._rpc_core.sweep_deferred_calls()
	api._rpc_core.sweep_transactions(api._receive_tick())
	return err


## Replaces [member NetwMultiplayer.inner] in place, rebinding the session to a
## new [SceneMultiplayer]. Reached through
## [method NetwEmbeddingHandle.adopt_inner].
func adopt_inner(new_inner: SceneMultiplayer) -> void:
	var api := _api()
	if api == null or new_inner == api.inner:
		return
	if new_inner.auth_callback.is_valid():
		api.auth_callback = new_inner.auth_callback
	api._unbind_inner_signals()
	api.inner = new_inner
	api._bind_inner_signals()
	api._session.adopt_inner(api.inner)


## Releases the whole graph the session owns, and is the one teardown call an
## embedding makes. Reached through [method NetwEmbeddingHandle.dispose].
func dispose() -> void:
	var api := _api()
	if api:
		api._release_owned_graph()


## Returns whether [method dispose] has begun a deliberate teardown, so the
## session machine can tell a local teardown from a server crash. Reached
## through [method NetwEmbeddingHandle.is_disposing].
func is_disposing() -> bool:
	var api := _api()
	return api._disposing if api else true
