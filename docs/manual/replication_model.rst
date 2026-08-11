.. _doc_manual_replication_model:

The replication model
=====================

Replication is the answer to one question asked every tick: which of this
entity's values changed, and which peers are owed the change. Networked answers
it by compiling each script's declarations once into a property set, and then
sending only what a set's trigger says is due to the peers interest admits.

This page covers the model. :ref:`doc_manual_authority_models` covers who is
allowed to write, and :ref:`doc_manual_identity_and_altitudes` covers what a
frame is addressed to. Everything here replicates a ``Node``. For rows that are
not nodes at all, such as two thousand mobs driven straight into
``RenderingServer``, see :ref:`doc_manual_replicated_tables`.

Declare once, per script
------------------------

Values are declared at ``_init`` time and compiled once for the whole script,
not once per instance. Two hundred instances of one enemy share one compiled
property set, one wire order, and one schema hash.

.. tabs::
 .. code-tab:: gdscript GDScript

    func _init() -> void:
        Netw.configure_property(self, &"position").state().epsilon(0.05)
        Netw.configure_property(self, &"health").state()
        Netw.configure_property(self, &"move_input").input()
        Netw.configure_property(self, &"aim_arrow").broadcast()

Declaration order is wire order, and the shape of the set is the shape of the
frame. That is why a set is *sealed*: once it has been compiled, adding a field
would change what every previously sent byte meant. A schema hash rides the
stream so a peer built against a different declaration is refused rather than
silently misreading.

Three record kinds
------------------

The kind decides one thing, and only one: whether the server keeps history for
the stream. It keeps history exactly when it must be able to second-guess the
author.

::

    STATE       the server's own truth, authored by it, kept in a rewind
                timeline. A body pose is the usual example.

    INPUT       a client's claim, sent to the server only, replayed by it to
                verify rather than believe, also kept for rewind. Movement
                keys are the usual example.

    BROADCAST   trusted display sent to everyone, checked by nobody, kept
                nowhere. An aim arrow is the usual example.

:ref:`BROADCAST <class_NetwMultiplayer_constant_BROADCAST>` is display truth
that lives entirely outside the rewind boundary and never grows a timeline. If
you find yourself wanting to validate a broadcast value, it should have been
:ref:`INPUT <class_NetwMultiplayer_constant_INPUT>`.

What causes a send
------------------

A set carries four independent facts about its traffic, set through
:ref:`property_set_set_param() <class_NetwMultiplayer_method_property_set_set_param>`
before the seal.

::

    trigger    what causes the set to be sent at all
    cadence    how often it is sent once triggered
    stamp      what tick information rides along, which is what lets a late
               frame be PLACED rather than merely applied
    channel    the carrier channel and whether delivery is reliable

Reliable delivery costs latency under loss, so a set sent every tick should
almost never be reliable. A dropped tick of a per-tick stream is repaired by the
next tick. A dropped reliable frame stalls everything behind it on that channel
while it retransmits.

Per-field, the two facts that matter most for bandwidth are the epsilon, which
suppresses a change too small to be worth a packet, and the quantizer, which
decides how many bits the value costs when it is sent.

Resolve, gate, dispatch
-----------------------

An entity frame is addressed to the session, not to a node. A Godot RPC is
addressed to a node, so it errors when the receiver has not spawned that node
yet. Networked addresses the route instead, which exists before any node does,
and resolves it on arrival.

::

    route state    arriving frame
    LIVE       ->  dispatch to the resolved node
    UNKNOWN    ->  reliable calls defer via when_live, otherwise drop (counted)
    LINGERING  ->  drop (counted)
    DEAD       ->  drop (counted)

Absence is normal while a spawn packet is still in flight, so it is a drop or a
short deferral, never an error. This is the single most important thing to
internalize about the receive path: **a healthy session drops packets.** Every
spawn edge and every despawn edge produces them.

Because healthy drops exist, they are counted rather than hidden.
:ref:`stats_snapshot() <class_NetwMultiplayer_method_stats_snapshot>` reports
per-reason counters, so a gap the next snapshot heals stays visible instead of
assumed::

    STAT_DROPS_UNKNOWN_ROUTE     the spawn has not landed yet
    STAT_DROPS_NOT_LIVE          the route is lingering or dead
    STAT_DROPS_SYNC_BAD_SENDER   the write policy refused the sender
    STAT_DROPS_SYNC_POISONED     the delta baseline is no longer trustworthy
    STAT_DROPS_SYNC_UNKNOWN_FLAG the frame used a flag this build lacks

The one to watch is a counter that keeps rising while nothing is spawning.
A spike at a scene change is the system working.

Sending: one flush per tick
---------------------------

Outgoing traffic is aggregated per peer and flushed once per tick rather than
sent per value. Ten changed properties on four entities bound for the same peer
become one packet, not forty. Both entity RPCs and property sync ride the same
aggregation and the same wire format, so a remote call and a state change to the
same peer in the same tick share a packet.

Interest gates the send, not the write. The server pumps only the entities a
peer's committed interest row admits, so an entity hidden from a peer costs that
peer nothing at all rather than costing a filtered packet. See
:ref:`doc_manual_interest_management`.

Discrete traffic
----------------

Continuous values are the sync path above. Discrete events are calls and
signals, declared the same way and carried on the same frames.

.. tabs::
 .. code-tab:: gdscript GDScript

    Netw.rpc(self, &"play_hit_effect", [position])   # fire and forget
    var reply := await Netw.request(self, &"ask_server", []).completed

A call whose target route is not live yet is parked rather than lost, when it
was sent reliably. That is the deferral row in the dispatch table, and it is why
a reliable spawn-adjacent call does not need a retry loop around it.

Where to go next
----------------

* :ref:`doc_manual_display_model` for how a received value reaches the screen.
* :ref:`doc_manual_prediction_model` for what the server keeps history for.
* :ref:`doc_manual_extending` for replacing the encode, decode, or admission
  stages.
