.. _doc_manual_prediction_model:

The prediction model
====================

Prediction makes local control responsive by advancing an entity before the
server confirms the result, and lag compensation lets the server judge an action
against the world the acting player actually saw. They are two halves of one
loop, and this page is the model underneath both.

:ref:`doc_manual_prediction_boundaries` is the practical guide to configuring a
predicted body. Read this page when you want to know what the system believes it
is computing, and why a disagreement is charged where it is.

One recurrence
--------------

Prediction is a single recurrence applied every tick::

    S' = F_H(S, C, E) -> (S', O)
      S  state        the declared state fields, compared against authority
      C  commands     the declared input fields, one set per tick
      E  environment  the declared sensor, epoch, and island facts
      H  topology     the schedule, participants, epoch, and reported body mode
      F  transition   the game's simulate callable, run under H
      O  witness      the realized contact classes and discrete solve facts

Each letter has exactly one source, which is what makes a disagreement
attributable rather than merely visible. ``S`` is what a property declared with
``.state()`` contributes, ``C`` what ``.input()`` contributes, ``E`` what the
entity's sensors and island declare, and ``F`` is the simulate callable run on
the declared schedule. ``H`` is captured by the engine rather than declared, and
``O`` is what the entity reports witnessing.

When a transition disagrees with authority, the disagreement is charged to one
antecedent and the configured recovery policy re-bases ``S``. Charging it to an
antecedent is the useful part: "we diverged" is not actionable, while "we
diverged because the input for that tick never arrived" is.

The input-to-state lifecycle
----------------------------

The owning client authors input every tick and predicts immediately, then ships
that input toward the server stamped with the tick it was authored for. The
server consumes one input per tick, so its frontier trails real time by about
half a round trip, and it records the produced state keyed by **the consumed
input tick** rather than by its own clock.

::

    client : author input[t] -> predict state[t] -> record input/state at t
                 |
                 |  ship input[t]  (stamped authored_tick = t)
                 v
    server : consume one input per tick, frontier trails by ~half a round trip
                 |
                 |  state[t] = simulate(input[t])
                 v
    timeline (server) keyed by the CONSUMED input tick t, never the clock
      |- input[t]  the exact command, never carries forward
      `- state[t]  the produced state, carried forward to later reads

That keying is the invariant every history read depends on. A read at a logical
tick returns the state that tick's input actually produced, so "where was this
entity at tick 15" has a defined answer even though no two peers agree on what
wall-clock moment tick 15 was.

The quantum
-----------

A transition is a fixed amount of simulated time, and two peers only mean the
same thing by one when they advance the world by the same amount inside it. This
is the antecedent no column can record, because it is the *width* of the
transition rather than anything inside it.

::

    a body the game integrates    one drive, one transition. Exact by
                                  construction, nothing to arm.

    a body the physics server     the server steps once per frame on its own
    integrates                    cadence, so a frame that emits no tick would
                                  advance the world by an amount no transition
                                  claims. Declaring the solver-body archetype
                                  holds that frame instead: no tick, no step,
                                  no transition.

The cost is worth stating plainly. A peer holds only the frames it has to spare,
so a peer whose physics cannot sustain the configured tickrate times its physics
steps per tick cannot hold anything and falls behind the authority it tracks.
:ref:`simulation_behind_count <class_NetwClockHandle_property_simulation_behind_count>`
reports exactly that. Running behind a host that is itself below budget means a
session in slow motion, which is what a single machine already does under load.
It is the consistent version of the same failure, not a new one.

The rewind boundary
-------------------

The server records exactly the two streams it must be able to second-guess:
:ref:`STATE <class_NetwMultiplayer_constant_STATE>`, its own authoritative
history, and :ref:`INPUT <class_NetwMultiplayer_constant_INPUT>`, the
controller's claim it replays to verify rather than believe. A stream whose
author is trusted outright records nothing, so a
:ref:`BROADCAST <class_NetwMultiplayer_constant_BROADCAST>` set is display truth
that lives entirely outside this boundary and never grows a timeline.

Reading history
---------------

:ref:`lagcomp_sample() <class_NetwMultiplayer_method_lagcomp_sample>` answers
"where was this entity when the shooter saw it". It returns the exact slot for
the requested tick once the consume frontier has reached it, and otherwise
carries the most recent prior slot forward. A tick ahead of the frontier
therefore reads the frontier, never a future the server has not simulated.

:ref:`lagcomp_rewind() <class_NetwMultiplayer_method_lagcomp_rewind>` is the
opt-in heavyweight that applies that past state to live nodes so a real physics
query can run against it. It costs more, so reach for the sample first and
rewind only when you genuinely need the engine's own intersection answer.

.. tabs::
 .. code-tab:: gdscript GDScript

    var api := NetwMultiplayer.of(self)
    var past := api.lagcomp_sample(target_entity, view_tick)
    if past.has_value(&"position"):
        validate_hit(origin, direction, past.position)

The tick to pass is the one the firing client displayed when it acted, which is
:ref:`display_get_tick() <class_NetwMultiplayer_method_display_get_tick>` on the
client. See :ref:`doc_manual_display_model`.

Action readiness
----------------

An action evaluated at a view tick agrees with the client only when two
independent conditions hold::

    1. availability : the consume frontier reached view_tick, so the engine
                      has consumed through it and the read is an exact slot
                      rather than a carried-forward one

    2. determinism  : predict(view_tick) == reconstruct(view_tick)

The default timing mode waits for neither and resolves on arrival. The
tick-aligned mode waits for availability. **Determinism is never waited on.** It
is a property of the placement contract, required only when the action places at
the owner's own predicted state, and simply absent when the action instead
validates server-recorded history of other entities.

Assumption layers
-----------------

Each capability adds only its own assumptions, so a game adopts the layer it
touches and no more. This is the part worth reading before deciding the addon is
too opinionated for your game.

* **None.** With no lag-compensation node mounted under the tree, the whole
  surface no-ops. A client-authoritative game carries nothing.
* **Sample and rewind** assume a shared logical tick, server per-tick recording,
  bounded retention, and that only the owning client predicts.
* **Actions**, in the default timing mode, add discrete-event and
  propose-and-validate semantics.
* **Tick-aligned actions** add the narrow assumptions that the action is
  owner-anchored, that the owner simulation is deterministic, and that
  resolution may be deferred behind the optimistic local effect.

Watching it work
----------------

The prediction counters on
:ref:`stats_snapshot() <class_NetwMultiplayer_method_stats_snapshot>` are the
first place to look when a predicted body feels wrong::

    STAT_PREDICT_CONSUMED         inputs the server actually consumed
    STAT_PREDICT_MISSING          ticks whose input never arrived in time
    STAT_PREDICT_CORRECTIONS      transitions that disagreed with authority
    STAT_PREDICT_MAX_REPLAY_DEPTH how far back a correction had to replay

A rising missing-input count is a network problem. A rising correction count
with inputs arriving is a determinism problem, and the two want completely
different fixes.

Where to go next
----------------

* :ref:`doc_manual_prediction_boundaries` for configuring an actual body.
* :ref:`doc_manual_display_model` for the visual that chases it.
* :ref:`doc_manual_extending` for replacing the drive, consume, evaluate, or
  recover stages.
