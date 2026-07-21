## Packed-column ring recording every transition a prediction engine drove, the
## instrument a deterministic kernel proves itself against.
##
## A row opens when a transition is authored and closes when its post-solve
## state is captured, so a closed row names one drive completely: the command
## that ran, the clock label it carried, and the fingerprint of the state it
## left. That makes the fingerprint the acceptance test rather than a tolerance.
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

## Which antecedent of the recurrence a divergence is charged to.
##
## A divergence whose antecedents the owner could test is always charged, because
## an unattributed one is a tuning problem and an attributed one is a bug with an
## address. Testing them takes authority's own command hash and environment
## digest, which arrive on the acknowledgement lane, so a divergence found before
## that lane has spoken is [constant UNATTRIBUTED] rather than blamed on the
## suspect that happens to be tested last.
enum Attribution {
	## No antecedent could be tested, so nothing is charged. The absence is
	## itself the finding: it says the acknowledgement lane had not covered this
	## transition when the divergence was found.
	UNATTRIBUTED,
	## The command differed. Authority ran input the owner did not author.
	COMMAND,
	## The environment differed. The transition ran against unequal world facts.
	ENVIRONMENT,
	## The simulation differed under equal command and equal environment.
	SIMULATION,
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
var _post_fps := PackedInt32Array()
var _kinds := PackedByteArray()
var _domains := PackedByteArray()
var _attributions := PackedByteArray()
var _flags := PackedByteArray()


func _init(capacity: int = CAPACITY_DEFAULT) -> void:
	_capacity = maxi(1, capacity)
	_transitions.resize(_capacity)
	_labels.resize(_capacity)
	_c_hashes.resize(_capacity)
	_e_digests.resize(_capacity)
	_post_fps.resize(_capacity)
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
## command bytes. The row stays open until [method close] records the state the
## drive produced.
func open(transition: int, label: int, kind: int, c_hash: int) -> void:
	var slot := _next_slot()
	_transitions[slot] = transition
	_labels[slot] = label
	_c_hashes[slot] = c_hash
	_e_digests[slot] = 0
	_post_fps[slot] = 0
	_kinds[slot] = kind
	_domains[slot] = Domain.IN_DOMAIN
	_attributions[slot] = Attribution.UNATTRIBUTED
	_flags[slot] = 0


## Closes the row for [param transition] with [param post_fp], the fingerprint
## of the canonical state the drive produced. Ignored when the row has already
## fallen out of the ring.
func close(transition: int, post_fp: int) -> void:
	var slot := _slot_of(transition)
	if slot < 0:
		return
	_post_fps[slot] = post_fp
	_flags[slot] |= ROW_CLOSED


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
## fingerprint of the declared world facts the drive ran against. Ignored when
## the row has already fallen out of the ring.
##
## It is what separates a divergence charged to
## [constant Attribution.ENVIRONMENT] from one charged to
## [constant Attribution.SIMULATION]. Equal commands and equal digests leave the
## simulation itself as the only remaining suspect.
func mark_e_digest(transition: int, e_digest: int) -> void:
	var slot := _slot_of(transition)
	if slot < 0:
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


## Returns the [enum Attribution] charged to [param transition], or
## [constant Attribution.UNATTRIBUTED] when nothing charged it or the row has
## fallen out of the ring.
func attribution_at(transition: int) -> Attribution:
	var slot := _slot_of(transition)
	if slot < 0:
		return Attribution.UNATTRIBUTED
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
##  ┖╴flags (int)          the ROW_ bitfield
## }
## [/codeblock]
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
		&"post_fp": _post_fps[slot],
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
	_start = (_start + 1) % _capacity
	return evicted


# Scans newest first, since every caller addresses a recent transition.
func _slot_of(transition: int) -> int:
	for i in range(_count - 1, -1, -1):
		var slot := (_start + i) % _capacity
		if _transitions[slot] == transition:
			return slot
	return -1
