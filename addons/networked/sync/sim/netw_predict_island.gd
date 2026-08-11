## The set of entities one predicted entity claims to simulate the way authority
## does, and how faithfully it simulates each.
##
## An island is the scope of a fingerprint compare. Naming participants is the
## game asserting something the engine is not entitled to assume -- that these
## bodies interact and that this peer reproduces the interaction -- and the
## claim is what admits the entity to exact comparison at all. An entity that
## names nobody is [constant NetwPredictJournal.Domain.OUT_OF_DOMAIN] on every
## transition, which is the absence of a claim rather than a claim of
## divergence.
##
## Membership and fidelity stay in one object because they are one fact.
## [method simulate] is [method add] plus a fidelity of
## [constant NetwPredict.Fidelity.SIMULATED], and a fidelity for a member the
## island does not have is not a declaration anyone can honor.
## [codeblock]
## var island := NetwEntity.of(self).prediction.island
## island.add(opponent)
## island.simulate(rival, _predict_rival_command)
## island.exact_claim = true
## [/codeblock]
##
## Membership is declared either explicitly, by naming entities, or produced,
## by resolving an interest layer. The two cannot be mixed with an exact claim:
## two peers each resolve their own interest scope, so a produced roster is not
## a fact both of them can claim, and an island that cannot claim it is compared
## by tolerance forever.
##
## [br][br]Membership is not simulation. Every member starts a witnessed
## [constant NetwPredict.Fidelity.PROXY], and [member promotion] decides which
## of the ones nobody named are stepped locally instead -- by count through
## [method simulate_nearest], or by radius through [method simulate_within].
## A member named by [method simulate] or [method observe] is decided already
## and no policy overrides it.
##
## [br][br]A scene may declare a rule its predicted descendants inherit through
## its own [member NetwPredictionHandle.island]. An inherited
## rule is the scene's, so an entity's own first declaration -- naming a member
## or a producer -- replaces it WHOLE rather than refining it. Writing only a
## tolerance or a reconcile mode onto an inherited rule refines the scene's
## rule, which is what a game writing only those asked for.
class_name NetwPredictIsland
extends RefCounted

## Whether this island is compared by tolerance rather than by exact
## fingerprint. Setting it clears [member exact_claim]; the two are the same
## question asked twice, and they may not disagree.
##
## Explicit membership defaults to exact comparison, because a roster the game
## wrote is a fact both peers can hold. Only production makes an island
## approximate, and [method from_interest] sets this itself.
var approximate: bool = false:
	set(value):
		approximate = value
		if value:
			exact_claim = false

## Whether this island claims its roster is a fact both peers hold, which is
## what admits exact comparison.
##
## A produced island cannot claim it, and the write is refused rather than
## stored: the claim would not be a fact about this island, it would be a
## statement the roster itself contradicts.
var exact_claim: bool = false:
	set(value):
		if value and not producers.is_empty():
			push_error(
				"NetwPredictIsland.exact_claim: produced islands are always "
				+ "approximate.",
			)
			return
		exact_claim = value
		if value:
			approximate = false

## Interest layers this island's membership is produced from, [code]&""[/code]
## meaning the entity's own resolved scope. Declared through
## [method from_interest].
var producers: Array[StringName] = []

## Entities explicitly named as island members, through [method add] and the
## verbs that call it.
var participants: Array[NetwEntity] = []

## Explicit [enum NetwPredict.Fidelity] overrides, by member. A member with no
## entry is decided by [member promotion].
var fidelity: Dictionary[NetwEntity, int] = { }

## Substituted command producers for simulated members, by member. Each is
## called as [code](participant, tick)[/code] and returns an input
## [Dictionary]; a member without one runs the zero-input COAST policy.
var command_predictors: Dictionary[NetwEntity, Callable] = { }

## Which produced members are promoted to local simulation.
var promotion: NetwPredict.Promotion = NetwPredict.Promotion.NONE

## The structural budget of [constant NetwPredict.Promotion.NEAREST], in members.
var promotion_count: int = 0

## The radius of [constant NetwPredict.Promotion.WITHIN], in metres.
var promotion_meters: float = 0.0

## When this island's members open transitions. The group owns the mechanism a
## pacing names, so this field selects one rather than performing it.
var pacing: NetwPredict.Pacing = NetwPredict.Pacing.SPECULATE

## Ticks a [constant NetwPredict.Pacing.DELAY_CLOSED] group holds each command
## before the transition it labels opens. The group schedules with it.
var input_delay_ticks: int = 0:
	set(value):
		input_delay_ticks = maxi(0, value)

## How divergence is reconciled across the island.
##
## Under [constant NetwPredict.Reconcile.JOINT] the group restores and replays
## its members together once a basis arrives. Admission is the engine's call at
## membership commit, where every member's schedule is known, so a JOINT
## declaration here is a request the engine may refuse by naming the member and
## the tier that cannot re-run.
var reconcile: NetwPredict.Reconcile = NetwPredict.Reconcile.INDEPENDENT

## Whether this rule came from the containing scene rather than
## from the entity's own declaration. An inherited rule yields to the entity's
## first own declaration and is re-inherited on reparent.
var inherited: bool = false

# The entity this island belongs to, for the scene-boundary check in add().
# A weak reference because the entity owns the handle that owns this.
var _owner_ref: WeakRef

## Whether this island claims a roster at all, and therefore whether the entity
## is admitted to the fingerprint compare.
##
## Derived rather than flagged: naming members or naming a producer IS the
## declaration, and nothing else plausibly means "compare me". Reading any other
## field must not opt an entity in, which a stored flag set by a fluent verb's
## first call could not promise.
var declared: bool:
	get:
		return not participants.is_empty() or not producers.is_empty()


## Produces members from the entity's resolved interest scope, or from
## [param layer] when one is named.
func from_interest(layer: StringName = &"") -> void:
	if exact_claim:
		push_error(
			"NetwPredictIsland.from_interest: produced islands cannot claim "
			+ "exact comparison.",
		)
		return
	_own()
	if layer not in producers:
		producers.append(layer)
	approximate = true


## Adds [param entity] as an explicit member.
func add(entity: NetwEntity) -> void:
	if not _is_same_scene(entity):
		push_error(
			"NetwPredictIsland.add: islands cannot cross scene "
			+ "boundaries.",
		)
		return
	_own()
	if entity not in participants:
		participants.append(entity)


## Removes [param entity] and everything declared about it.
func remove(entity: NetwEntity) -> void:
	participants.erase(entity)
	fidelity.erase(entity)
	command_predictors.erase(entity)


## Promotes [param entity] for local simulation. When [param predict_commands]
## is valid it is installed as that member's command producer.
func simulate(
		entity: NetwEntity,
		predict_commands: Callable = Callable(),
) -> void:
	add(entity)
	_set_fidelity(entity, NetwPredict.Fidelity.SIMULATED)
	if predict_commands.is_valid():
		self.predict_commands(entity, predict_commands)


## Keeps [param entity] as a displayed proxy, exempt from promotion policy.
func observe(entity: NetwEntity) -> void:
	add(entity)
	_set_fidelity(entity, NetwPredict.Fidelity.PROXY)


## Installs [param predictor] as the substituted command producer for
## [param entity].
func predict_commands(entity: NetwEntity, predictor: Callable) -> void:
	add(entity)
	if not predictor.is_valid():
		push_error(
			"NetwPredictIsland.predict_commands: predictor must be a valid "
			+ "Callable.",
		)
		return
	if entity in participants:
		command_predictors[entity] = predictor


## Promotes the nearest [param count] produced members for local simulation.
func simulate_nearest(count: int) -> void:
	promotion = NetwPredict.Promotion.NEAREST
	promotion_count = maxi(0, count)


## Promotes produced members within [param meters] for local simulation.
func simulate_within(meters: float) -> void:
	promotion = NetwPredict.Promotion.WITHIN
	promotion_meters = maxf(0.0, meters)


## Promotes every produced member for local simulation.
func simulate_all() -> void:
	promotion = NetwPredict.Promotion.ALL


# Takes ownership of an inherited scene rule, which the entity's first own
# declaration replaces WHOLE rather than refines. Naming a member or a producer
# is that declaration, and those are the only two acts that can be it, so this
# is where the replacement happens. A tolerance or a reconcile mode written onto
# an inherited rule refines the scene's rule, which is what a game that wrote
# only those asked for.
func _own() -> void:
	if not inherited:
		return
	inherited = false
	producers.clear()
	participants.clear()
	fidelity.clear()
	command_predictors.clear()
	promotion = NetwPredict.Promotion.NONE
	promotion_count = 0
	promotion_meters = 0.0
	pacing = NetwPredict.Pacing.SPECULATE
	input_delay_ticks = 0


# Binds the owning entity, which only add() reads. Called by the handle when it
# binds and whenever an island is installed on it.
func _bind(owner: NetwEntity) -> void:
	_owner_ref = weakref(owner) if owner else null


# A detached copy marked inherited, or null when this rule names nothing for a
# descendant to take. A rule that only sets a tolerance says nothing about who
# is being compared, so it does not opt a descendant into a compare.
func _inheritable() -> NetwPredictIsland:
	if not declared:
		return null
	var copy := _duplicate()
	copy.inherited = true
	return copy


# A detached copy, for handing a scene rule to one descendant entity.
func _duplicate() -> NetwPredictIsland:
	var copy := NetwPredictIsland.new()
	copy.producers = producers.duplicate()
	copy.participants = participants.duplicate()
	copy.fidelity = fidelity.duplicate()
	copy.command_predictors = command_predictors.duplicate()
	copy.promotion = promotion
	copy.promotion_count = promotion_count
	copy.promotion_meters = promotion_meters
	copy.pacing = pacing
	copy.input_delay_ticks = input_delay_ticks
	# Assigned through the setters last, so the copy resolves the same
	# invariants the original did rather than inheriting a half-set pair.
	copy.approximate = approximate
	copy.exact_claim = exact_claim
	copy.reconcile = reconcile
	return copy


# Stores one fidelity override, for a member the island actually has.
func _set_fidelity(entity: NetwEntity, value: NetwPredict.Fidelity) -> void:
	if entity in participants:
		fidelity[entity] = value


# Whether a candidate member sits in the same scene as the owner. An unbound
# island answers true: it has no scene to be crossed. The comparison is on the
# resolved scene RID, since a handle is a view and two views of one scene are
# distinct objects.
func _is_same_scene(participant: NetwEntity) -> bool:
	if not participant or not is_instance_valid(participant):
		return false
	var owner := _owner_ref.get_ref() as NetwEntity if _owner_ref else null
	if not owner:
		return true
	return owner.scene.entity == participant.scene.entity
