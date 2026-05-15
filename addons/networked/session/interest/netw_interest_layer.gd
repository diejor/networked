## Membership relationship between a set of peers (viewers) and a set
## of entities (subjects), composed according to its [enum Policy].
##
## Created via [method NetwInterest.create_layer]. Layers do not own
## the entities they contain; freeing a layer with [method dispose]
## detaches it from the registry but leaves entities and peers alive.
## [codeblock]
##     var arena := ctx.interest.create_layer(
##             &"combat:1", NetwInterestLayer.Policy.ISOLATE)
##     arena.add_participant(player_a)
##     arena.add_participant(player_b)
## [/codeblock]
##
## Layer mutations are coalesced by [NetwInterest] and applied in a
## single deferred flush per frame, so adding many members/subjects
## in one frame does not produce per-call visibility traffic.
class_name NetwInterestLayer
extends RefCounted


## Visibility contribution model. Composition across layers proceeds
## by starting at [code]false[/code], OR-ing in any [code]GRANT[/code]
## or [code]ISOLATE[/code] "+visible" verdict, then forcing
## [code]false[/code] for any [code]DENY[/code] or [code]ISOLATE[/code]
## "-blocked" verdict.
## [br][br]
## - [code]GRANT[/code]: members additionally see subjects (additive).
## [br]- [code]DENY[/code]: subjects are hidden from co-members.
## [br]- [code]ISOLATE[/code]: bubble; members see only subjects, and
## subjects are seen only by members.
enum Policy { GRANT, DENY, ISOLATE }


## Stable identifier passed to [method NetwInterest.create_layer].
var id: StringName

## The composition policy assigned at creation. Immutable.
var policy: Policy = Policy.GRANT


## Emitted when a peer is added to [member members].
signal member_added(peer_id: int)

## Emitted when a peer is removed from [member members].
signal member_removed(peer_id: int)

## Emitted when an entity is added to [member subjects].
signal subject_added(entity: NetwEntity)

## Emitted when an entity is removed from [member subjects].
signal subject_removed(entity: NetwEntity)


## Emitted after a flush when [param peer_id] gains visibility on
## [param entity] specifically because of this layer's contribution.
signal interest_enter(entity: NetwEntity, peer_id: int)

## Emitted after a flush when [param peer_id] loses visibility on
## [param entity] specifically because of this layer's contribution.
signal interest_exit(entity: NetwEntity, peer_id: int)


## Emitted by [method dispose] before visibility is torn down.
## Connect to run cleanup that must complete first (e.g. fade
## animations). Handlers defer the close by calling
## [method NetwInterestLayer.Ack.defer] and invoking the returned
## [Callable] when their async work finishes.
signal closing(ack: Ack)

## Emitted after [signal closing] settles and visibility has been
## removed. The layer is no longer registered after this fires.
signal closed


var _members: Dictionary[int, bool] = {}
var _subjects: Dictionary[NetwEntity, bool] = {}
var _interest: WeakRef
var _disposed: bool = false

## When [code]true[/code], this layer is a read-only client mirror
## maintained by [InterestService]. Server-side mutators
## ([method add_member], [method add_subject], etc.) become no-ops.
var _is_mirror: bool = false


func _init(
		id_: StringName,
		policy_: Policy,
		interest: NetwInterest,
) -> void:
	id = id_
	policy = policy_
	_interest = weakref(interest)


## Adds [param peer_id] to [member members]. Idempotent. Schedules a
## flush.
##
## On a mirror layer this is a no-op; mirror state is updated by
## [InterestService].
func add_member(peer_id: int) -> void:
	if _disposed or peer_id == 0 or _is_mirror:
		return
	if _members.has(peer_id):
		return
	_members[peer_id] = true
	var ix := _get_interest()
	if ix:
		ix._on_member_added(self, peer_id)
		var svc := ix._service()
		if svc:
			svc.notify_member_added(self, peer_id)
	member_added.emit(peer_id)


## Removes [param peer_id] from [member members]. Idempotent.
func remove_member(peer_id: int) -> void:
	if _is_mirror:
		return
	if not _members.has(peer_id):
		return
	_members.erase(peer_id)
	var ix := _get_interest()
	if ix:
		ix._on_member_removed(self, peer_id)
		var svc := ix._service()
		if svc:
			svc.notify_member_removed(id, peer_id)
	member_removed.emit(peer_id)


## Adds [param entity] to [member subjects]. Idempotent. Server-only
## (subjects do not replicate to clients).
func add_subject(entity: NetwEntity) -> void:
	if _disposed or not is_instance_valid(entity) or _is_mirror:
		return
	if _subjects.has(entity):
		return
	_subjects[entity] = true
	entity._on_layer_subscribed(self)
	var ix := _get_interest()
	if ix:
		ix._on_subject_added(self, entity)
	subject_added.emit(entity)


## Removes [param entity] from [member subjects]. Idempotent.
func remove_subject(entity: NetwEntity) -> void:
	if _is_mirror:
		return
	if not _subjects.has(entity):
		return
	_subjects.erase(entity)
	if is_instance_valid(entity):
		entity._on_layer_unsubscribed(self)
	var ix := _get_interest()
	if ix:
		ix._on_subject_removed(self, entity)
	subject_removed.emit(entity)


## Adds [param entity] as both a subject and, when
## [member NetwEntity.peer_id] is non-zero, a member. The common case
## for [code]ISOLATE[/code] participants.
func add_participant(entity: NetwEntity) -> void:
	if not is_instance_valid(entity):
		return
	add_subject(entity)
	if entity.peer_id != 0:
		add_member(entity.peer_id)


## Inverse of [method add_participant]. Removes [param entity] from
## both [member subjects] and [member members].
func remove_participant(entity: NetwEntity) -> void:
	if not is_instance_valid(entity):
		return
	remove_subject(entity)
	if entity.peer_id != 0:
		remove_member(entity.peer_id)


## Returns [code]true[/code] if [param peer_id] is in [member members].
func has_member(peer_id: int) -> bool:
	return _members.has(peer_id)


## Returns [code]true[/code] if [param entity] is in [member subjects].
func has_subject(entity: NetwEntity) -> bool:
	return _subjects.has(entity)


## Snapshot of current members.
func members() -> Array[int]:
	var out: Array[int] = []
	out.assign(_members.keys())
	return out


## Snapshot of current subjects.
func subjects() -> Array[NetwEntity]:
	var out: Array[NetwEntity] = []
	out.assign(_subjects.keys())
	return out


## Returns [code]true[/code] after [method dispose] has finished, or
## while [method dispose_immediate] is tearing the layer down.
func is_disposed() -> bool:
	return _disposed


## Two-phase teardown. Emits [signal closing] with an [Ack], awaits
## any deferred handlers, removes visibility, emits [signal closed],
## and unregisters from [NetwInterest].
func dispose() -> void:
	if _disposed:
		return
	var ack := Ack.new()
	closing.emit(ack)
	while not ack.is_settled():
		var tree := Engine.get_main_loop() as SceneTree
		if tree:
			await tree.process_frame
		else:
			break
	_finish_disposal()


## Tears the layer down without firing [signal closing]. Use when game
## code has nothing to clean up.
func dispose_immediate() -> void:
	if _disposed:
		return
	_finish_disposal()


func _finish_disposal() -> void:
	if _is_mirror:
		_client_finish_dispose()
		return
	_disposed = true
	for entity in _subjects.keys():
		if is_instance_valid(entity):
			entity._on_layer_unsubscribed(self)
	var ix := _get_interest()
	if ix:
		ix._on_layer_disposed(self)
		var svc := ix._service()
		if svc:
			svc.notify_layer_disposed(id)
	_members.clear()
	_subjects.clear()
	closed.emit()


# ---------------------------------------------------------------------------
# Client-mirror update hooks. Called by InterestService when authority
# RPCs land. Only emit signals; do not forward to NetwInterest (the server
# is the only authority that drives visibility computation).
# ---------------------------------------------------------------------------

func _client_apply_member_added(peer_id: int) -> void:
	if _members.has(peer_id):
		return
	_members[peer_id] = true
	member_added.emit(peer_id)


func _client_apply_member_removed(peer_id: int) -> void:
	if not _members.has(peer_id):
		return
	_members.erase(peer_id)
	member_removed.emit(peer_id)


func _client_finish_dispose() -> void:
	if _disposed:
		return
	_disposed = true
	_members.clear()
	_subjects.clear()
	closed.emit()


func _get_interest() -> NetwInterest:
	return _interest.get_ref() if _interest else null


## Acknowledgement token passed to [signal closing] handlers.
##
## Each handler that needs to complete async work calls [method defer]
## to receive a release [Callable], runs its work, then invokes the
## callable. [method NetwInterestLayer.dispose] waits until every
## deferred handler has released before tearing visibility down.
## [codeblock]
##     layer.closing.connect(func(ack):
##         var done := ack.defer()
##         await play_outro()
##         done.call())
## [/codeblock]
class Ack extends RefCounted:
	var _pending: int = 0

	## Reserves a deferred slot and returns the release [Callable].
	## Call the returned [Callable] exactly once when async work is
	## complete.
	func defer() -> Callable:
		_pending += 1
		return _release

	## Returns [code]true[/code] when every deferred slot has been
	## released.
	func is_settled() -> bool:
		return _pending <= 0

	func _release() -> void:
		_pending -= 1
