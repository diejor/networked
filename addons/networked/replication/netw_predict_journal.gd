## Packed-column ring recording every transition a prediction engine drove, the
## instrument a deterministic kernel proves itself against.
##
## A row opens when a transition is authored and closes when its post-solve
## state is captured, so a closed row names one drive completely. Its evidence
## is immutable after close while verdict flags, attribution, and domain may
## settle later. That makes the fingerprint the acceptance test rather than a
## tolerance.
## Two peers that ran the same transition closure agree on [method post_fps] at
## every acked transition, and the first transition where they disagree is the
## one that broke, not the one where the error grew visible.
## [codeblock]
## var journal := entity.prediction.journal()
## var transitions := journal.transitions()
## var flags := journal.flags()
## for i in transitions.size():
##     if flags[i] & NetwPredictJournal.ROW_DIVERGENT:
##         print("diverged at transition ", transitions[i])
##         break
## [/codeblock]
class_name NetwPredictJournal
extends RefCounted

## How many transitions a journal retains before the oldest row falls off.
const CAPACITY_DEFAULT := 256

## The row has been acknowledged by authority. Set by [method mark_ack].
const ROW_ACKED := 1

## The acknowledged authority state fingerprinted equal to [method post_fps].
const ROW_MATCHED := 2

## Authority ran a command the owner never authored for this transition.
const ROW_SUBSTITUTED := 4

## A later acknowledgement replaced this row's verdict with a truer one.
const ROW_SUPERSEDED := 8

## The acknowledged authority state fingerprinted unequal to [method post_fps].
const ROW_DIVERGENT := 16

## The row's drive has produced its state, so [method post_fps] carries a real
## fingerprint rather than the zero an open row holds. Set by [method close] and
## by [method mark_substituted], whose row is final without ever having run.
const ROW_CLOSED := 32

## The row's [method pre_fps] did not chain from the preceding produced state
## and no operator provenance accounts for the intervening write.
const ROW_CHAIN_BROKEN := 64

## Both peers declared a realized witness for the row and its fingerprint
## matched. The local [code]witness_detail[/code] may therefore describe the
## peer's stable witness facts too.
const ROW_WITNESS_MATCHED := 128

## The row carries a declared realized-transition witness.
const EVIDENCE_WITNESS := 1

## The row carries the optional raw pre-state fingerprint.
const EVIDENCE_RAW := 2

## Which antecedent of the recurrence a divergence is charged to.
##
## A divergence is charged to the first unequal boundary in causal order:
## pre-state, command, environment, topology, optional raw execution bits,
## realized contact, then the produced closure. Missing required peer evidence
## yields [constant UNKNOWN] rather than a guess.
enum Attribution {
	## Required peer evidence was absent, so no boundary is blamed.
	UNKNOWN,
	## The peers entered the transition with different declared state.
	PRE_STATE,
	## The command differed. Authority ran input the owner did not author.
	COMMAND,
	## The environment differed. The transition ran against unequal world facts.
	ENVIRONMENT,
	## The execution topology or body mode differed.
	TOPOLOGY,
	## Canonical state agreed but optional raw execution bits differed.
	EXECUTION,
	## Equal observed antecedents produced a different contact witness.
	CONTACT,
	## Every observed antecedent agreed but the produced state differed.
	CLOSURE,
}

## Recovery operators that may account for a state-chain discontinuity.
enum Operator {
	## No operator wrote the body since the preceding row.
	NONE,
	## A projected authoritative basis was restored.
	REBASE_PROJECTED,
	## An exact authoritative basis was restored.
	REBASE_EXACT,
	## The teleport tier restored the full declared closure.
	FULL_CLOSURE,
	## Present-time moving transport composed an aligned delta.
	TRANSPORT_DELTA,
	## Fallback seeded the body from authority.
	RESEED,
	## The first post-reseed comparison aligned the acknowledgement horizon.
	RESEED_ALIGN,
	## A witnessed boundary breach closed speculation without closing input.
	DEMOTE,
	## A clean momentum-only divergence was left to contract without a write.
	DISSIPATE,
}

## State family that first differed inside a pre-state or closure boundary.
enum StateFamily {
	## No family differed or the peer evidence was unavailable.
	NONE,
	## Position and orientation fields.
	POSE,
	## Declared derivative and velocity fields.
	MOMENTUM,
	## Remaining controller state and latches.
	CONTROLLER_LATCH,
}

## Whether a transition is one the peers are entitled to reproduce exactly.
enum Domain {
	## Every antecedent of the transition is declared equal on both peers, so
	## its fingerprints must match exactly.
	IN_DOMAIN,
	## An antecedent is declared unequal or unknown, so the transition is
	## compared by tolerance rather than by fingerprint.
	OUT_OF_DOMAIN,
}

const _FNV_OFFSET := 2166136261
const _FNV_PRIME := 16777619
const _U32 := 0xFFFFFFFF
const _I32_SIGN := 0x80000000
const _U32_SPAN := 0x100000000

var _capacity: int
var _epoch: int = -1
var _count: int = 0
var _start: int = 0

var _transitions := PackedInt64Array()
var _labels := PackedInt64Array()
var _c_hashes := PackedInt32Array()
var _e_digests := PackedInt32Array()
var _pre_fps := PackedInt32Array()
var _topo_fps := PackedInt32Array()
var _raw_fps := PackedInt32Array()
var _witness_fps := PackedInt32Array()
var _witness_class_bits := PackedByteArray()
var _aligned_errors := PackedFloat32Array()
var _post_fps := PackedInt32Array()
var _pre_pose_fps := PackedInt32Array()
var _pre_momentum_fps := PackedInt32Array()
var _pre_controller_fps := PackedInt32Array()
var _post_pose_fps := PackedInt32Array()
var _post_momentum_fps := PackedInt32Array()
var _post_controller_fps := PackedInt32Array()
var _episode_ids := PackedInt32Array()
var _write_ids := PackedInt32Array()
var _operators := PackedByteArray()
var _bases := PackedInt64Array()
var _differing_families := PackedByteArray()
var _evidence_masks := PackedByteArray()
var _kinds := PackedByteArray()
var _domains := PackedByteArray()
var _attributions := PackedByteArray()
var _flags := PackedByteArray()
var _witness_details: Dictionary[int, Dictionary] = { }


func _init(capacity: int = CAPACITY_DEFAULT) -> void:
	_capacity = maxi(1, capacity)
	_transitions.resize(_capacity)
	_labels.resize(_capacity)
	_c_hashes.resize(_capacity)
	_e_digests.resize(_capacity)
	_pre_fps.resize(_capacity)
	_topo_fps.resize(_capacity)
	_raw_fps.resize(_capacity)
	_witness_fps.resize(_capacity)
	_witness_class_bits.resize(_capacity)
	_aligned_errors.resize(_capacity)
	_post_fps.resize(_capacity)
	_pre_pose_fps.resize(_capacity)
	_pre_momentum_fps.resize(_capacity)
	_pre_controller_fps.resize(_capacity)
	_post_pose_fps.resize(_capacity)
	_post_momentum_fps.resize(_capacity)
	_post_controller_fps.resize(_capacity)
	_episode_ids.resize(_capacity)
	_write_ids.resize(_capacity)
	_operators.resize(_capacity)
	_bases.resize(_capacity)
	_differing_families.resize(_capacity)
	_evidence_masks.resize(_capacity)
	_kinds.resize(_capacity)
	_domains.resize(_capacity)
	_attributions.resize(_capacity)
	_flags.resize(_capacity)


## Returns the fingerprint of [param bytes] as fnv1a-32, the one hash the
## journal, the wire, and every later comparison share.
##
## The result is a signed 32-bit integer because that is the width the columns
## and the wire both carry, so a fingerprint never changes representation
## between being computed, stored, and sent.
static func fnv1a(bytes: PackedByteArray) -> int:
	var h := _FNV_OFFSET
	for b in bytes:
		h = ((h ^ b) * _FNV_PRIME) & _U32
	return h - _U32_SPAN if h >= _I32_SIGN else h


## Opens the row for [param transition], recording the command that is about to
## drive it.
##
## [param label] is the clock label the drive carries, used only for display and
## for flooring the input window. [param kind] is a
## [enum NetwLagCompensationInterface.PredictionHandle.DriveKind] value naming
## how the command was selected. [param c_hash] fingerprints the canonical
## command bytes. [param pre_fp] fingerprints the declared state consumed by the
## drive. [param pre_families] carries pose, momentum, then controller and latch
## fingerprints. [param provenance] names an operator write since the preceding
## row. The row stays open until [method close] records the produced state. A
## retained row with the same transition is left unchanged.
func open(
		transition: int,
		label: int,
		kind: int,
		c_hash: int,
		pre_fp: int = 0,
		pre_families: PackedInt32Array = PackedInt32Array(),
		provenance: Dictionary = { },
		topo_fp: int = 0,
		raw_fp: int = 0,
		evidence_mask: int = 0,
) -> void:
	if _slot_of(transition) >= 0:
		return
	var slot := _next_slot()
	_transitions[slot] = transition
	_labels[slot] = label
	_c_hashes[slot] = c_hash
	_e_digests[slot] = 0
	_pre_fps[slot] = pre_fp
	_topo_fps[slot] = topo_fp
	_raw_fps[slot] = raw_fp
	_witness_fps[slot] = 0
	_witness_class_bits[slot] = 0
	_aligned_errors[slot] = 0.0
	_post_fps[slot] = 0
	_pre_pose_fps[slot] = _family_at(pre_families, 0)
	_pre_momentum_fps[slot] = _family_at(pre_families, 1)
	_pre_controller_fps[slot] = _family_at(pre_families, 2)
	_post_pose_fps[slot] = 0
	_post_momentum_fps[slot] = 0
	_post_controller_fps[slot] = 0
	_episode_ids[slot] = int(provenance.get(&"episode", 0))
	_write_ids[slot] = int(provenance.get(&"write_id", 0))
	_operators[slot] = int(provenance.get(&"operator", Operator.NONE))
	_bases[slot] = int(provenance.get(&"basis", -1))
	_differing_families[slot] = StateFamily.NONE
	_evidence_masks[slot] = evidence_mask
	_kinds[slot] = kind
	_domains[slot] = Domain.IN_DOMAIN
	_attributions[slot] = Attribution.UNKNOWN
	_flags[slot] = 0


## Seals the execution topology and realized witness for [param transition]
## before [method close]. [param evidence_mask] declares which optional columns
## are present. [param detail] is debug-only witness narration retained with the
## row and never used for equality. [param witness_class_bits] is the compact,
## peer-invariant authority summary carried by acknowledgement records.
func mark_solve(
		transition: int,
		topo_fp: int,
		witness_fp: int,
		evidence_mask: int,
		detail: Dictionary = { },
		witness_class_bits: int = 0,
) -> void:
	var slot := _slot_of(transition)
	if slot < 0 or _flags[slot] & ROW_CLOSED:
		return
	_topo_fps[slot] = topo_fp
	_witness_fps[slot] = witness_fp
	_witness_class_bits[slot] = witness_class_bits
	_evidence_masks[slot] = evidence_mask
	if not detail.is_empty():
		_witness_details[transition] = detail.duplicate(true)


## Closes the row for [param transition] with [param post_fp], the fingerprint
## of the canonical state the drive produced. [param post_families] carries its
## pose, momentum, then controller and latch fingerprints. Ignored when the row
## has already closed or fallen out of the ring.
func close(
		transition: int,
		post_fp: int,
		post_families: PackedInt32Array = PackedInt32Array(),
) -> void:
	var slot := _slot_of(transition)
	if slot < 0 or _flags[slot] & ROW_CLOSED:
		return
	_post_fps[slot] = post_fp
	_post_pose_fps[slot] = _family_at(post_families, 0)
	_post_momentum_fps[slot] = _family_at(post_families, 1)
	_post_controller_fps[slot] = _family_at(post_families, 2)
	_flags[slot] |= ROW_CLOSED


## Marks [param transition] as a state-chain break with no operator provenance.
## Ignored when the row has already fallen out of the ring.
func mark_chain_broken(transition: int) -> void:
	var slot := _slot_of(transition)
	if slot >= 0:
		_flags[slot] |= ROW_CHAIN_BROKEN


## Records authority's verdict for [param transition]: [constant ROW_ACKED]
## always, then [constant ROW_MATCHED] or [constant ROW_DIVERGENT] from
## [param matched]. Ignored when the row has already fallen out of the ring.
func mark_ack(transition: int, matched: bool) -> void:
	var slot := _slot_of(transition)
	if slot < 0:
		return
	var flags := _flags[slot] | ROW_ACKED
	flags &= ~(ROW_MATCHED | ROW_DIVERGENT)
	_flags[slot] = flags | (ROW_MATCHED if matched else ROW_DIVERGENT)


## Records whether both peers supplied equal realized-transition witnesses.
## Ignored when the row has already fallen out of the ring.
func mark_witness_match(transition: int, matched: bool) -> void:
	var slot := _slot_of(transition)
	if slot < 0:
		return
	_flags[slot] &= ~ROW_WITNESS_MATCHED
	if matched:
		_flags[slot] |= ROW_WITNESS_MATCHED


## Marks [param transition] as one authority ran with a command the owner never
## authored. Ignored when the row has already fallen out of the ring.
##
## Substitution is recorded rather than inferred, because an owner that has to
## guess whether its command ran cannot tell a substituted transition from a
## simulation that diverged on its own.
func mark_substituted(transition: int) -> void:
	var slot := _slot_of(transition)
	if slot < 0:
		return
	_flags[slot] |= ROW_SUBSTITUTED | ROW_CLOSED
	# A transition authority ran with a command its owner never authored has an
	# unequal antecedent by definition, so it is never entitled to exactness.
	_domains[slot] = Domain.OUT_OF_DOMAIN


## Marks a retained [param transition] as superseded by a later substitution.
## Its sealed evidence remains unchanged while the verdict overlay records
## [constant ROW_SUBSTITUTED] and [constant ROW_SUPERSEDED]. Ignored when the
## row has already fallen out of the ring.
func mark_superseded(transition: int) -> void:
	var slot := _slot_of(transition)
	if slot < 0:
		return
	_flags[slot] |= ROW_SUBSTITUTED | ROW_SUPERSEDED
	_domains[slot] = Domain.OUT_OF_DOMAIN


## Labels [param transition] with the [enum Domain] its antecedents earn.
## Ignored when the row has already fallen out of the ring.
##
## A row opens [constant Domain.IN_DOMAIN] because that is the claim whose
## failure is loud: an in-domain transition that disagrees is a bug with an
## address, while an out-of-domain one only ever asks for tolerance. Facts that
## arrive after the drive, a substituted acknowledgement above all, downgrade
## the row rather than the row having waited for them.
func mark_domain(transition: int, domain: Domain) -> void:
	var slot := _slot_of(transition)
	if slot < 0:
		return
	_domains[slot] = domain


## Records the environment digest [param e_digest] for [param transition], the
## fingerprint of the declared world facts the drive ran against. Ignored after
## the row closes or when it has already fallen out of the ring.
##
## It separates [constant Attribution.ENVIRONMENT] from later causal boundaries.
func mark_e_digest(transition: int, e_digest: int) -> void:
	var slot := _slot_of(transition)
	if slot < 0 or _flags[slot] & ROW_CLOSED:
		return
	_e_digests[slot] = e_digest


## Charges [param transition]'s divergence to [param attribution]. Ignored when
## the row has already fallen out of the ring.
##
## The charge lives on the row rather than only on the newest verdict, so a
## comparison that answers for an older transition still reads the charge that
## transition earned instead of whatever the acknowledgement lane judged last.
func mark_attribution(transition: int, attribution: Attribution) -> void:
	var slot := _slot_of(transition)
	if slot < 0:
		return
	_attributions[slot] = attribution


## Records the aligned state error measured when [param transition] settles.
## Ignored after ring eviction.
func mark_aligned_error(transition: int, error: float) -> void:
	var slot := _slot_of(transition)
	if slot >= 0:
		_aligned_errors[slot] = error


## Records which state family first differed inside [param transition]'s
## pre-state or closure boundary. Ignored after ring eviction.
func mark_differing_family(
		transition: int,
		family: StateFamily,
) -> void:
	var slot := _slot_of(transition)
	if slot >= 0:
		_differing_families[slot] = family


## Returns the [enum Attribution] charged to [param transition], or
## [constant Attribution.UNKNOWN] when nothing charged it or the row has
## fallen out of the ring.
func attribution_at(transition: int) -> Attribution:
	var slot := _slot_of(transition)
	if slot < 0:
		return Attribution.UNKNOWN
	return _attributions[slot] as Attribution


## Returns the newest transition whose row is [constant ROW_CLOSED] with every
## older retained row closed too, or [code]-1[/code] when none is.
##
## This is the frontier an acknowledgement may claim. A row that is open holds a
## zero fingerprint, so acknowledging it would assert a state the drive has not
## produced yet.
func last_closed() -> int:
	var newest := -1
	for i in _count:
		var slot := (_start + i) % _capacity
		if not (_flags[slot] & ROW_CLOSED):
			break
		newest = _transitions[slot]
	return newest


## Returns the oldest transition whose row is not [constant ROW_MATCHED], or
## [code]-1[/code] when every retained row matched. This is the transition a
## recovery rebases at.
func first_unmatched() -> int:
	for i in _count:
		var slot := (_start + i) % _capacity
		if not (_flags[slot] & ROW_MATCHED):
			return _transitions[slot]
	return -1


## Returns the row recorded for [param transition], or an empty [Dictionary]
## when it has fallen out of the ring.
##
## This is the tooling-cadence read. Bulk readers take the columns through
## [method transitions] and its siblings instead of materializing a row per
## transition.
## [codeblock]
## {
##  ┠╴transition (int)     the recurrence index, injective within an epoch
##  ┠╴label (int)          the clock label the drive carried
##  ┠╴kind (int)           a DriveKind value naming the command selection
##  ┠╴c_hash (int)         fingerprint of the canonical command bytes
##  ┠╴e_digest (int)       fingerprint of the declared environment facts
##  ┠╴post_fp (int)        fingerprint of the state the drive produced
##  ┠╴domain (int)         a Domain value
##  ┠╴attribution (int)    an Attribution value
##  ┖╴flags (int)          the ROW_ bitfield
## }
## [/codeblock]
## The row also exposes [code]pre_fp[/code], the pre and post
## [code]pose_fp[/code], [code]momentum_fp[/code], and
## [code]controller_fp[/code] families, plus the preceding operator's
## [code]episode_id[/code], [code]write_id[/code], [code]operator[/code], and
## [code]basis[/code]. Settled rows expose [code]aligned_error[/code], and
## witnessed rows expose [code]witness_class_bits[/code].
func row_at(transition: int) -> Dictionary:
	var slot := _slot_of(transition)
	if slot < 0:
		return { }
	return {
		&"transition": _transitions[slot],
		&"label": _labels[slot],
		&"kind": _kinds[slot],
		&"c_hash": _c_hashes[slot],
		&"e_digest": _e_digests[slot],
		&"pre_fp": _pre_fps[slot],
		&"topo_fp": _topo_fps[slot],
		&"raw_fp": _raw_fps[slot],
		&"witness_fp": _witness_fps[slot],
		&"witness_class_bits": _witness_class_bits[slot],
		&"aligned_error": _aligned_errors[slot],
		&"evidence_mask": _evidence_masks[slot],
		&"witness_detail": _witness_details.get(transition, { }).duplicate(true),
		&"pre_pose_fp": _pre_pose_fps[slot],
		&"pre_momentum_fp": _pre_momentum_fps[slot],
		&"pre_controller_fp": _pre_controller_fps[slot],
		&"post_fp": _post_fps[slot],
		&"post_pose_fp": _post_pose_fps[slot],
		&"post_momentum_fp": _post_momentum_fps[slot],
		&"post_controller_fp": _post_controller_fps[slot],
		&"episode_id": _episode_ids[slot],
		&"write_id": _write_ids[slot],
		&"operator": _operators[slot],
		&"basis": _bases[slot],
		&"differing_family": _differing_families[slot],
		&"domain": _domains[slot],
		&"attribution": _attributions[slot],
		&"flags": _flags[slot],
	}


## Returns the [enum Domain] recorded for [param transition], or
## [constant Domain.OUT_OF_DOMAIN] when the row has fallen out of the ring.
##
## A transition nobody retained is one nobody can show was entitled to
## exactness, so the forgotten row reads as the label that claims nothing.
func domain_at(transition: int) -> Domain:
	var slot := _slot_of(transition)
	return Domain.OUT_OF_DOMAIN if slot < 0 else _domains[slot] as Domain


## Returns the [code]ROW_*[/code] bitfield recorded for [param transition], or
## [code]0[/code] when the row has fallen out of the ring.
##
## This and [method domain_at] are the per-transition reads the compare path
## makes on every authoritative frame, which is why they answer with a scalar
## instead of going through [method row_at]. Materializing a row dictionary to
## read one byte out of it allocates once per frame per predicted entity.
func flags_at(transition: int) -> int:
	var slot := _slot_of(transition)
	return 0 if slot < 0 else _flags[slot]


## Returns the retained transitions oldest first. Every other column accessor
## returns values in this same order, so index [code]i[/code] names one row
## across all of them.
func transitions() -> PackedInt64Array:
	var out := PackedInt64Array()
	out.resize(_count)
	for i in _count:
		out[i] = _transitions[(_start + i) % _capacity]
	return out


## Returns the retained clock labels, ordered by [method transitions].
func labels() -> PackedInt64Array:
	var out := PackedInt64Array()
	out.resize(_count)
	for i in _count:
		out[i] = _labels[(_start + i) % _capacity]
	return out


## Returns the retained command fingerprints, ordered by [method transitions].
func c_hashes() -> PackedInt32Array:
	var out := PackedInt32Array()
	out.resize(_count)
	for i in _count:
		out[i] = _c_hashes[(_start + i) % _capacity]
	return out


## Returns the retained environment digests, ordered by [method transitions].
## A row whose engine declares no environment facts carries [code]0[/code].
func e_digests() -> PackedInt32Array:
	var out := PackedInt32Array()
	out.resize(_count)
	for i in _count:
		out[i] = _e_digests[(_start + i) % _capacity]
	return out


## Returns the retained pre-state fingerprints, ordered by [method transitions].
func pre_fps() -> PackedInt32Array:
	return _ordered_i32(_pre_fps)


## Returns the retained execution-topology fingerprints, ordered by
## [method transitions].
func topo_fps() -> PackedInt32Array:
	return _ordered_i32(_topo_fps)


## Returns the retained raw pre-state fingerprints, ordered by
## [method transitions]. A row without [constant EVIDENCE_RAW] carries zero.
func raw_fps() -> PackedInt32Array:
	return _ordered_i32(_raw_fps)


## Returns the retained realized-witness fingerprints, ordered by
## [method transitions]. A row without [constant EVIDENCE_WITNESS] carries zero.
func witness_fps() -> PackedInt32Array:
	return _ordered_i32(_witness_fps)


## Returns authority witness-class summaries ordered by [method transitions].
func witness_class_bits() -> PackedByteArray:
	return _ordered_u8(_witness_class_bits)


## Returns pose, momentum, then controller and latch fingerprints per retained
## pre-state, ordered by [method transitions].
func pre_family_fps() -> PackedInt32Array:
	return _ordered_families(
		_pre_pose_fps,
		_pre_momentum_fps,
		_pre_controller_fps,
	)


## Returns the retained post-solve state fingerprints, ordered by
## [method transitions]. An open row carries [code]0[/code] until
## [method close].
func post_fps() -> PackedInt32Array:
	var out := PackedInt32Array()
	out.resize(_count)
	for i in _count:
		out[i] = _post_fps[(_start + i) % _capacity]
	return out


## Returns the retained
## [enum NetwLagCompensationInterface.PredictionHandle.DriveKind] values,
## ordered by [method transitions].
func kinds() -> PackedByteArray:
	var out := PackedByteArray()
	out.resize(_count)
	for i in _count:
		out[i] = _kinds[(_start + i) % _capacity]
	return out


## Returns the retained [enum Domain] values, ordered by [method transitions].
func domains() -> PackedByteArray:
	var out := PackedByteArray()
	out.resize(_count)
	for i in _count:
		out[i] = _domains[(_start + i) % _capacity]
	return out


## Returns pose, momentum, then controller and latch fingerprints per retained
## post-state, ordered by [method transitions].
func post_family_fps() -> PackedInt32Array:
	return _ordered_families(
		_post_pose_fps,
		_post_momentum_fps,
		_post_controller_fps,
	)


## Returns the first retained transition marked [constant ROW_CHAIN_BROKEN], or
## [code]-1[/code] when the retained chain is intact.
func first_chain_break() -> int:
	for i in _count:
		var slot := (_start + i) % _capacity
		if _flags[slot] & ROW_CHAIN_BROKEN:
			return _transitions[slot]
	return -1


## Returns the retained divergence charges, ordered by [method transitions].
func attributions() -> PackedByteArray:
	var out := PackedByteArray()
	out.resize(_count)
	for i in _count:
		out[i] = _attributions[(_start + i) % _capacity]
	return out


## Returns the retained evidence-completeness masks, ordered by
## [method transitions].
func evidence_masks() -> PackedByteArray:
	var out := PackedByteArray()
	out.resize(_count)
	for i in _count:
		out[i] = _evidence_masks[(_start + i) % _capacity]
	return out


## Returns the retained [code]ROW_[/code] bitfields, ordered by
## [method transitions].
func flags() -> PackedByteArray:
	var out := PackedByteArray()
	out.resize(_count)
	for i in _count:
		out[i] = _flags[(_start + i) % _capacity]
	return out


## Drops every retained row and adopts [param epoch].
##
## Transitions are injective only within one epoch, so a journal that outlives
## an epoch would hold two rows claiming the same transition. Clearing is how a
## rewire keeps [method row_at] unambiguous.
func clear(epoch: int) -> void:
	_epoch = epoch
	_count = 0
	_start = 0
	_witness_details.clear()


## Returns how many rows the journal currently retains, at most
## [method capacity].
func size() -> int:
	return _count


## Returns how many rows the journal retains before the oldest falls off.
func capacity() -> int:
	return _capacity


## Returns the epoch [method clear] last adopted, or [code]-1[/code] before any.
func epoch() -> int:
	return _epoch


# Claims the next ring slot, evicting the oldest row once the ring is full.
func _next_slot() -> int:
	if _count < _capacity:
		var slot := (_start + _count) % _capacity
		_count += 1
		return slot
	var evicted := _start
	_witness_details.erase(_transitions[evicted])
	_start = (_start + 1) % _capacity
	return evicted


# Scans newest first, since every caller addresses a recent transition.
func _slot_of(transition: int) -> int:
	for i in range(_count - 1, -1, -1):
		var slot := (_start + i) % _capacity
		if _transitions[slot] == transition:
			return slot
	return -1


static func _family_at(families: PackedInt32Array, index: int) -> int:
	return families[index] if index < families.size() else 0


func _ordered_i32(column: PackedInt32Array) -> PackedInt32Array:
	var out := PackedInt32Array()
	out.resize(_count)
	for i in _count:
		out[i] = column[(_start + i) % _capacity]
	return out


func _ordered_u8(column: PackedByteArray) -> PackedByteArray:
	var out := PackedByteArray()
	out.resize(_count)
	for i in _count:
		out[i] = column[(_start + i) % _capacity]
	return out


func _ordered_families(
		pose: PackedInt32Array,
		momentum: PackedInt32Array,
		controller: PackedInt32Array,
) -> PackedInt32Array:
	var out := PackedInt32Array()
	out.resize(_count * 3)
	for i in _count:
		var slot := (_start + i) % _capacity
		out[i * 3] = pose[slot]
		out[i * 3 + 1] = momentum[slot]
		out[i * 3 + 2] = controller[slot]
	return out
