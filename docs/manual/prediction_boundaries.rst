.. _doc_manual_prediction_boundaries:

Prediction boundaries
=====================

Prediction makes local control responsive by advancing an entity before the
server confirms the result. The predicted state is a claim, not authority.
Networked compares that claim with the server row for the transition the
server acknowledged, then applies the configured recovery when the difference
is actionable.

The prediction boundary names everything this peer steps as part of that
claim. The controlling entity is always inside it. An island may add selected
remote entities. A contact with anything else is an observed boundary breach.
This distinction matters most for solver bodies, where contact changes future
momentum and a small pose error can grow after the contact ends.

Start with the body
-------------------

Add a :ref:`PredictionComponent <class_PredictionComponent>` once on the
entity root and choose the archetype that matches its simulation:

* ``KINEMATIC`` is for a callable step that can be replayed several times in
  one frame. It uses tick scheduling and rebase plus replay.
* ``SOLVER_BODY`` is for a body integrated by the physics solver once per
  frame. It uses frame scheduling, projected rebases, and predicts through
  witnessed breaches by default.

An archetype is a baseline. Declare exceptional facts on the entity's stable
``prediction`` handle. Declare field facts on the field itself:

.. tabs::
 .. code-tab:: gdscript GDScript

    func _init() -> void:
        var entity := NetwEntity.ensure(self)
        Netw.configure_property(self, &"position").state().epsilon(0.05) \
            .interpolate(
                NetwInterpolate.new().lerp().project_by(&"linear_velocity"),
            )
        Netw.configure_property(self, &"linear_velocity").state() \
            .epsilon(0.5).teleport_only()

        entity.prediction.archetype(
            NetwLagCompensationInterface.PredictionHandle.Archetype.SOLVER_BODY,
        )
        entity.prediction.schedule().frame().hold_repeat_last()

The fluent prediction verbs are grouped by the fact they declare:

* ``schedule()`` selects tick or frame cadence and missing input behavior.
* ``recovery()`` selects the recovery strategy and breach response.
* ``sensors()`` captures world facts the simulation reads.
* ``witness()`` captures realized contacts and other boundary evidence.
* ``transport()`` declares when a present time pose offset may be composed.
* ``island()`` produces participants and promotes selected members for local
  simulation.
* ``archetype()`` and ``epoch()`` set the body baseline and world version.

Keep one source for each fact. A value exported by ``PredictionComponent``
must not also be declared through the handle. Networked reports the conflict
and keeps the scene value.

The boundary model
------------------

Each predicted transition depends on five kinds of evidence:

* State is the entity state before and after the transition.
* Command is the local, received, or substituted input that drove it.
* Environment is every world fact outside the prediction boundary.
* Witness is what the local solve actually contacted or observed.
* Topology is the schedule, island membership, fidelity, and world epoch.

The environment cannot be recreated from state alone. Networked therefore
does not promise deterministic client physics. It records the witness and
topology beside the transition, compares peer invariant evidence, and charges
a disagreement to the earliest fact it can prove.

The command horizon is bounded. When authority has not acknowledged progress,
the client stops adding speculative transitions at the structural history
limit while it continues sampling input and resending the outstanding command
window. Read ``ack_age_max``, ``ack_age_ticks``, ``authoring_clamped``, and
``speculation_held`` from ``prediction.stats()`` when diagnosing this state.

How much time a transition is worth
-----------------------------------

A transition is also a fixed amount of simulated time, and that is the one
antecedent no column above can record, because it is the width of the
transition rather than anything inside it. Two peers only mean the same thing
by a transition when they advance the world by the same amount inside it.

For a body the game integrates this is exact by construction: one drive, one
transition, nothing to arm. For a body the physics server integrates it is not.
The server steps once per frame on its own cadence, and a client whose clock is
throttled to track a slower host keeps rendering and stepping at its own rate,
so every frame that emits no tick advances the world by an amount no transition
claims. The error is continuous, it compounds, and every compared column stays
equal while it happens.

Declaring ``Archetype.SOLVER_BODY`` arms a simulation gate for that body's
world. A frame that emits no tick then runs no solve and opens no transition.
Commands keep flowing on a held frame, because holding simulated time must
never hold input.

The cost is worth stating. A peer can only hold frames it has to spare, so a
peer whose physics cannot sustain ``tickrate`` times ``physics_steps_per_tick``
steps per wall second cannot hold anything and falls behind the authority it
tracks; ``simulation_behind_count`` on the clock reports it. Running behind a
host that is itself below its frame budget means a session in slow motion,
which is what a single machine already does under load. The gate makes that
consistent between peers rather than letting the client race ahead and fork.

``physics_ticks_per_second`` must be a whole multiple of ``tickrate`` for a
solver body. One step cannot be split, so a fractional ratio alternates on a
phase each peer keeps privately and no other declaration repairs it. Networked
reports that configuration against the entity that drives under it.

Read ``quantum_steps``, ``quantum_declared``, and ``quantum_faults`` from
``prediction.stats()``. A nonzero fault count is the one divergence cause a
peer detects alone, before any comparison disagrees, and a cross-peer mismatch
is charged to ``TOPOLOGY`` rather than exhausting the attribution ladder.

This couples the meaning of a tick and nothing else. Deterministic lockstep
couples the players' commands and costs a round trip of input latency before
anything moves. Commands here stay speculative and stay substitutable.

Choose the smallest sufficient rung
-----------------------------------

The recovery gradient is progressive endogenization. Each rung moves more of
the environment inside the local prediction boundary or reacts earlier to the
part that remains outside it.

.. list-table:: Prediction boundary gradient
   :header-rows: 1
   :widths: 16 28 30 26

   * - Rung
     - Declaration
     - Use when
     - Cost
   * - Tolerant prediction
     - ``archetype()`` and field ``epsilon()`` marks
     - Contact is benign and ordinary correction contracts the error.
     - Lowest CPU and no added display latency.
   * - Witnessed demotion
     - ``witness()`` and ``recovery().on_breach(DEMOTE)``
     - Solver contact can amplify error after the contact.
     - Contact is shown at authority display latency until a clean proof
       permits prediction to resume.
   * - Present time transport
     - ``transport().corridor(...)``
     - Pose offset is the error and the corridor is known to be contact clean.
     - One present time write. It cannot repair momentum or hidden solver state.
   * - Simulated participant
     - ``island().from_interest().simulate_nearest(n)``
     - A nearby remote body causes phantom contacts as a delayed proxy.
     - One extra body step per promoted member and command prediction error.
   * - Joint reconciliation
     - ``island().reconcile(JOINT)``
     - Reserved. The interaction must pass the usefulness test below.
     - Joint state history and replay for every member.

Do not climb the gradient merely to reduce a meter. Choose the first rung that
removes a player visible failure in the game's actual interaction regime.

Witnessed demotion
------------------

A solver body should declare the world facts its transition reads and the
contacts its solve realizes. A ground sensor usually identifies ordinary
support, while a contact witness distinguishes support from static geometry
and dynamic entities.

.. tabs::
 .. code-tab:: gdscript GDScript

    func _init() -> void:
        var prediction := NetwEntity.ensure(self).prediction
        prediction.sensors().sample(&"ground", sample_ground)
        prediction.witness().contacts(sample_contacts)
        prediction.recovery().on_breach(
            NetwLagCompensationInterface \
                .PredictionHandle.BreachResponse.DEMOTE,
        )

A breach demotes at the witness transition. Local commands keep flowing, but
the display follows received authority until consecutive authority witness
rows prove a clean interval. The proof length scales with measured
acknowledgement age. Display role transitions retain the last visible output
and absorb the source change through the interpolation chase.

``SOLVER_BODY`` predicts through by default. Demotion is explicit because its
authority-followed display adds acknowledgement and interpolation latency. A
body whose collider receives newer authority state than its buffered visual
can otherwise appear detached. ``KINEMATIC`` and witness free entities also
keep predicting through ordinary correction.

The recovery ladder may attempt a no write ``DISSIPATE`` stage for a clean,
aligned momentum only divergence. It watches the existing meter for a bounded
window. Contraction closes the episode. Non contraction spends one budget
unit, then the ordinary rebase stage proceeds. This is an engine decision, not
a tuning knob.

Produced islands and fidelity
-----------------------------

Island declarations are rules because participants join and leave at runtime.
``from_interest()`` produces entities that share the subject's resolved local
interest scope. Passing a layer name narrows the producer. ``add(entity)`` and
``remove(entity)`` are the explicit form for gameplay code that holds entity
references. Producers compose as a union and never cross scene boundaries.

Membership does not imply simulation. Every member starts as a ``PROXY``.
Promotion policies select the ``SIMULATED`` subset:

.. tabs::
 .. code-tab:: gdscript GDScript

    entity.prediction.island() \
        .approximate() \
        .from_interest() \
        .simulate_nearest(1)

    entity.prediction.island().simulate(opponent, predict_opponent_command)

Nearest and radius policies use local distance and hysteresis. Membership and
fidelity changes commit at transition boundaries. A fidelity change waits
while the subject and member are in realized contact, because changing the
collider representation during contact would create a new physics fork.

A simulated remote publishes predicted input and speculative simulation. It
runs its normal simulation callback through the same schedule tier. The
default substituted command is ``COAST``, a fresh zero input that lets existing
momentum and friction carry the body. A per participant predictor may return a
partial input dictionary over that zero baseline.

Each received authority state independently rebases the simulated body.
Extrapolated restore projects declared pose channels by replicated velocity,
bounded by ``max_restore_ticks``. No recovery episode or budget is opened for
this continuous correction. The remaining error is bounded by receive cadence
times command prediction error.

The displayed body is the collidable body. A simulated remote therefore uses
the predicted display chase. Contact with it is inside the boundary and does
not demote the subject. Contact detail can still be wrong when the substituted
command differs from the remote command.

Scene defaults
--------------

Use ``MultiplayerScene.prediction_island()`` to apply one produced rule to
predicted entities in a scene:

.. tabs::
 .. code-tab:: gdscript GDScript

    scene.prediction_island() \
        .approximate() \
        .from_interest(&"race") \
        .simulate_nearest(1)

An entity's own ``island()`` declaration replaces the scene island rule.
Sensors and epoch are separate environment facts and remain intact. Produced
islands are approximate because peers may evaluate a threshold on different
display samples. Exact comparison is available only for explicit membership.

The joint usefulness test
-------------------------

``JOINT`` reconciliation is intentionally reserved. It becomes useful only
when all three statements are true:

#. Contact is decisive. Its exact result changes gameplay, not only appearance.
#. Contact is error sensitive. Independent rebases cannot keep the result
   within the game's accepted tolerance.
#. Scope is small. The interacting set can be bounded tightly enough to store
   and replay as one unit.

If any statement is false, use independent reconciliation. A racing game where
bumping is cosmetic does not earn joint replay. A fighting game where a two
body collision decides a hit may earn it after measurement. Calling
``reconcile(JOINT)`` currently reports the unmet gate and keeps independent
reconciliation.

Costs and failure shapes
------------------------

Each mechanism pays in a different currency:

* Replay spends CPU in proportion to unacknowledged commands and requires a
  simulation callable that is safe to run repeatedly.
* Solver rebases avoid replay but expose correction energy. Projection reduces
  stale placement but depends on replicated derivative channels.
* Demotion bounds wrong simulation time at the cost of authority display
  latency during the witnessed interval.
* Simulated participants spend one local body step per promoted member. Their
  residual depends on authority receive cadence and command prediction error.
* Wider interest increases replication bandwidth before it increases island
  capacity. An island cannot simulate an entity whose state stream does not
  exist on the peer.
* Tighter epsilon values increase correction and episode frequency. They do
  not improve the information available to the client.

Termination and honesty are invariants, not knobs. The command horizon,
episode evidence horizon, escalation budget, and authority clean resume proof
stay structurally bounded. Configuration controls perception and cost within
those bounds.

Diagnostics
-----------

Use the stable handle surfaces instead of reading engine internals:

* ``prediction.stats()`` reports resolved axes, schedule, acknowledgement age,
  command pressure, correction counts, and produced island membership.
* ``prediction.journal()`` returns transition evidence and operator outcomes.
* ``prediction.episode()`` returns the current or last disturbance record.
* ``prediction.metrics()`` exposes aggregate counters for monitors.

In a debug build, enable **Debug > Visible Collision Shapes** to draw the
prediction-boundary overlay. The project setting
``debug/networked/prediction_boundary_overlay`` can disable it independently.
Four in-world rings show the present-time layers for every speculative entity:

* cyan ``D`` is the displayed pose;
* green ``P`` is the live predicted state;
* orange ``A`` is the newest received authority row;
* magenta ``B`` is the settled comparison basis while an episode is open.

The gaps make display error, the unsupervised horizon, and aligned comparison
error visible without capturing another stream. Authority pose and tick come
from the existing interpolation buffer. The label reports witness and
breach evidence, episode boundaries, operator outcomes, the contraction meter,
demotion state, acknowledgement age, display lag, and displayed authoring tick.
Demoted entities use a red label tint.

When a predicted body looks wrong, ask these questions in order:

#. Did command authoring reach ``ack_age_max``?
#. Does the compared row name ``STATE``, ``ENVIRONMENT``, ``TOPOLOGY``, or
   ``CONTACT`` as the first divergent fact?
#. Was the contact inside the simulated island or against a proxy?
#. Did the selected operator contract the aligned error?
#. Is the remaining problem simulation error or only display lag?

Interest decides whether the remote state stream exists. Prediction decides
which admitted entities this peer steps and how it recovers. Interpolation
decides how those state changes become visible.
