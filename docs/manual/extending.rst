.. _doc_manual_extending:

Extending the engine
====================

Every intake verdict and engine stage in Networked is an overridable method with
a working default. You install a subclass of
:ref:`NetwMultiplayer <class_NetwMultiplayer>`, override the decisions your game
owns, and call ``super()`` for the stock behavior. There is one seam, not a
family of extension classes, and each method is independently overridable, so
replacing one stage never obliges you to replace another.

Try a layer first
-----------------

Most rules people reach for an override to express are not engine replacements.
They are compositions on top of the stock engine, and the stock engine already
composes.

:ref:`layer_create() <class_NetwMultiplayer_method_layer_create>` with a policy,
a leave policy, and a driver callback expresses "hide this from them" without
replacing the interest matrix at all. A team room, a fog radius, a spectator
carve-out, and a private instance are all layers.

.. tabs::
 .. code-tab:: gdscript GDScript

    var arena := api.layer_create(&"arena")
    api.layer_add_entity(arena, player_entity)
    api.layer_add_viewer(arena, player_peer_id)

Reach for the stages below when you are **replacing an engine**, not when you
are adding a rule. If your override would end up calling ``super()`` and then
filtering the result, a layer was the right tool.

Gates and stages
----------------

The seam splits into two kinds of method, and the split falls out of *when* the
method runs relative to route resolution.

::

    gate    a verdict on remote bytes, judged before the wire route resolves
            to an entity, so it keys on route and never on RID

    stage   the work itself, keyed by entity: RID, already resolved

That is why a gate cannot take an RID. At the moment a gate runs there may not
be an entity yet, which is exactly the case a gate exists to rule on.

The families
------------

::

    gates        _sync_admit_frame, _spawn_admit_frame, _predict_admit_frame

    sync         _entity_add_field_set, _entity_remove_field_set,
                 _sync_encode, _sync_decode, _note_ack, _note_sent

    sync         _gather_set, _apply_set
    property
    boundary

    spawn        _spawn_declare, _spawn_undeclare, _spawn_reconcile,
                 _spawn_construct

    prediction   _predict_drive, _predict_consume, _predict_evaluate,
                 _predict_recover

    interest     _layer_declare, _layer_undeclare, _interest_recompute,
                 _interest_commit, _interest_row_of, _interest_admits,
                 _interest_explain

    display      _display_declare, _display_undeclare, _display_record,
                 _display_pump_entity, _display_write

:ref:`_gather_set() <class_NetwMultiplayer_private_method__gather_set>` and
:ref:`_apply_set() <class_NetwMultiplayer_private_method__apply_set>` are worth
calling out. They are the sync lane's whole property boundary: everything
replication reads off your nodes goes through one, and everything it writes
back goes through the other. Overriding that pair redirects replication away
from node properties altogether, which is the hook for a game whose real state
does not live on nodes.

The lane is what the pair covers, not the session. The simulation plane
(prediction, and
:ref:`lagcomp_rewind() <class_NetwMultiplayer_method_lagcomp_rewind>`) writes
nodes through its own slot-bound port, because a rewind moves an object the
sync lane declared no set for and has to put it back whatever the body did. An
override installed here sees replication traffic and does not see a rewind.

Override a family together
--------------------------

Interest's seven stages share one committed matrix, and display's five share
per-entity sample history. Replacing part of either family leaves the rest
reading state your override never fills.

**Nothing enforces this.** Override the family together, or forward the members
you do not care about.

.. tabs::
 .. code-tab:: gdscript GDScript

    extends NetwMultiplayer

    func _interest_recompute() -> Error:
        return _my_own_fold()

    func _interest_admits(entity: RID, peer_bit: int) -> bool:
        return _my_matrix.test(entity, peer_bit)

The other families are genuinely independent. Overriding
:ref:`_sync_encode() <class_NetwMultiplayer_private_method__sync_encode>` alone
is fine, as long as the matching decode still understands what it produced.

Installing your subclass
------------------------

Three install points, from broadest to narrowest. The narrower one wins.

::

    project setting   MULTIPLAYER_SCRIPT_SETTING names the default script
                      for every tree in the project

    per tree          MultiplayerTree.api_script overrides it for one tree

    per call          NetwMultiplayer.make() constructs one directly, which
                      is what test rigs use

.. tabs::
 .. code-tab:: gdscript GDScript

    # Per tree, in the scene that owns the session.
    $MultiplayerTree.api_script = preload("res://net/my_multiplayer.gd")

The library holds no opinion about override shape beyond this. There is no
virtual group you must implement all of, no registration call, and no base class
other than :ref:`NetwMultiplayer <class_NetwMultiplayer>` itself. A subclass
that overrides nothing behaves exactly like the stock engine, which is what
makes an incremental override safe to land.

Call super, and mean it
-----------------------

Two habits keep an override maintainable across upgrades.

**Call** ``super()`` **unless you are truly replacing the stage.** A gate that
adds one refusal should ask ``super()`` first and only then apply its own rule,
so every refusal the stock gate learns in a later version still applies.

.. tabs::
 .. code-tab:: gdscript GDScript

    func _sync_admit_frame(route: int, sender: int) -> Error:
        var verdict := super(route, sender)
        if verdict != OK:
            return verdict
        return OK if _my_rule_allows(route, sender) else ERR_UNAUTHORIZED

**Count what you refuse.** The stock engine counts every drop by reason, and an
override that silently returns a refusal makes traffic disappear with no counter
moving. That is the single hardest class of bug to diagnose in a live session.
Emit your own counter, or reuse a stat, but do not drop silently.

Verifying an override
---------------------

An override is worth exactly as much as its evidence. Two checks are cheap and
catch most of what goes wrong:

* Run the full suite with your extension installed. The laws are written to
  parameterize by environment rather than by loop, so they certify a subclass
  the same way they certify the stock engine.
* Compare
  :ref:`stats_snapshot() <class_NetwMultiplayer_method_stats_snapshot>` before
  and after. An override that changed a number you did not intend to change is
  the finding.

See :ref:`doc_manual_testing` for the harness.

Where to go next
----------------

* :ref:`doc_manual_identity_and_altitudes` for what an RID and a route mean at
  the seam.
* :ref:`doc_manual_replication_model` for the stages the sync family replaces.
* :ref:`doc_manual_interest_management` for the layer surface to try first.
