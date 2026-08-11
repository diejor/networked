.. _doc_manual_display_model:

The display model
=================

Snapshots land every few ticks and jittery, so the display never shows the
newest one raw. Networked plays the visual back behind the live state by a
buffer delay, so there is always a later snapshot to interpolate toward and
motion stays smooth across a gap. The trade is a fixed display latency of
roughly that buffer depth.

This is the display half of a networked entity. Prediction owns the body, the
display owns only what the body looks like. Keeping them separate is what lets a
body be snapped hard by a correction while the thing the player is watching
glides.

The playhead
------------

Each tracked value keeps a ring buffer of recorded snapshots keyed by tick. A
playhead reads that buffer behind the newest entry, lands between two recorded
ticks, and the displayed value is the interpolation between them.

::

    history (one per value), keyed by tick:
       9       12       15       18       21        newest received = 21
                        |---- playhead ---|
                        prev = 15   dt = 16.4   next = 18

    displayed = lerp(state[15], state[18], 0.4)

Once per frame the playhead is placed in the history's tick space from the
display clock, then every value lerps across the two ticks bracketing it::

    time := display_tick + tick_factor - display_lag
    dt := floori(time)              # the playhead tick
    factor := time - dt             # progress from dt toward dt + 1
    every value: write lerp(history[prev], history[next], factor)

:ref:`display_tick <class_NetwClockHandle_property_display_tick>` already trails
the simulation by
:ref:`display_offset <class_NetwClockHandle_property_display_offset>`, and the
per-entity display lag subtracts the extra jitter buffer on top of that. An
empty history is never written, so the pump defers to whatever last wrote the
property instead of clobbering it with a stale value.

Four display roles
------------------

The same entity is displayed one of four ways. The role is resolved per entity
from control, authority, and the authored sync channels, unless you override it
through
:ref:`DISPLAY_PARAM_ROLE <class_NetwMultiplayer_constant_DISPLAY_PARAM_ROLE>`.
The question every rule answers is where the displayed values come from.

::

    # resolved per entity
    if role was set explicitly:
        use it
    if predicting and controlled locally:
        PREDICTED   we simulate it live; chase the body
    if we are its authority and control it locally:
        DISABLED    our own body is truth; keep raw input feel
    if every feeding sync channel is authored here:
        AUTHORITY   we author the stream; sample our own simulation
    if we are its authority:
        DISABLED    nothing streams this; the body is truth
    otherwise:
        REMOTE      someone else's stream; play it back

:ref:`DISABLED <class_NetwMultiplayer_constant_DISABLED>` is not a failure mode.
Your own locally controlled body should not be smoothed, because smoothing it
would put a filter between the player's input and what they see, which is
exactly the latency the whole system exists to hide.

Smart dilation
--------------

The buffer depth adapts. The display lag eases toward a floor set by the
expected snapshot interval and the clock display offset. When snapshots starve,
no recorded tick sits after the playhead, so the lag grows to rebuild the buffer
and settles back once packets resume. Turning smart dilation off through
:ref:`DISPLAY_PARAM_SMART_DILATION <class_NetwMultiplayer_constant_DISPLAY_PARAM_SMART_DILATION>`
gives a fixed zero lag, which is the right choice only when you control the
network conditions.

The gauges that tell you whether the buffer is healthy are on
:ref:`stats_snapshot() <class_NetwMultiplayer_method_stats_snapshot>`::

    STAT_DISPLAY_STARVING          entities with no tick after the playhead
    STAT_DISPLAY_MAX_DISPLAY_LAG   the deepest buffer any entity is holding
    STAT_DISPLAY_SNAPS             smoothing bypassed, a visible pop

Predicted display
-----------------

A locally predicted entity has no jitter buffer to drain, because its body is
simulated live every frame and races ahead of the network. Two filters are
available through
:ref:`DISPLAY_PARAM_PREDICTED_MODE <class_NetwMultiplayer_constant_DISPLAY_PARAM_PREDICTED_MODE>`:

* :ref:`CHASE <class_NetwMultiplayer_constant_CHASE>` eases the visual toward
  the live body on an exponential time. It is the one role that reads the body
  instead of history.
* :ref:`BRACKETED <class_NetwMultiplayer_constant_BRACKETED>` records the
  predicted body each tick and interpolates the previous and current samples
  exactly like a remote entity. It is the remote pipeline fed by a local
  sampler.

Writing the visual
------------------

Smoothing must not fight the physics engine. Point
:ref:`DISPLAY_PARAM_VISUAL_ROOT <class_NetwMultiplayer_constant_DISPLAY_PARAM_VISUAL_ROOT>`
at a child that carries the smooth output, while the body keeps the raw pose
that replication writes. Which of the two write-out modes you get is chosen by
the visual's ``top_level`` flag.

.. tabs::
 .. code-tab:: gdscript GDScript

    # Parented visual: smooth position, inherit the rest from the body.
    entity.interpolation.visual_root = NodePath("Sprite")

    # Detached ghost: smooth the whole pose, inherit nothing.
    $Visual.top_level = true
    entity.interpolation.visual_root = NodePath("Visual")

A parented visual takes global-space writes for the spatial channels, so a
replicated write to the body never drags them along through transform
inheritance, while every channel *without* a smoothing spec still inherits from
the body for free. A facing flip, an animation bob, and a riding attachment all
come along at no cost. A top-level visual is a detached ghost: the parent never
composes, every written channel is absolute, nothing inherits, and any channel
can be smoothed including the transform itself.

Without a visual root, and for non-spatial values, the write is plain. Teleports
bypass smoothing entirely through
:ref:`display_snap() <class_NetwMultiplayer_method_display_snap>`, which is what
you want whenever the new value is not continuous with the old one.

Naming the displayed tick
-------------------------

When a stamped frame drives the entity, history is keyed by the frame's
authoring tick rather than the receive tick. That lets
:ref:`display_get_tick() <class_NetwMultiplayer_method_display_get_tick>` name the
exact server tick under the playhead, which is the world a firing client
actually saw.

::

    packet { __tick = 15, position = ... }   recorded at history key 15
    playhead under tick 15   ->   display_get_tick() == 15
    the shooter sends 15; the server rewinds every target to tick 15

That tick is half a round trip behind the live server state, which is precisely
why it is the right number to send to the server for lag-compensated validation.
See :ref:`doc_manual_prediction_model`.

Smoothing event arguments
-------------------------

Discrete events carry continuous values too. A smoothing spec per argument on
:ref:`configure_rpc() <class_Netw_method_configure_rpc>` or
:ref:`configure_signal() <class_Netw_method_configure_signal>` glides that
argument into a property while the handler still runs, so a periodic aim update
does not arrive as a jump. Unlike a property, an argument has no implicit
destination, so the target must be named.

Where to go next
----------------

* :ref:`doc_manual_prediction_model` for the body the display is chasing.
* :ref:`doc_manual_replication_model` for what fills the history in the first
  place.
* :ref:`doc_manual_extending` for replacing the record, pump, or write stages.
