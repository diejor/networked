.. _doc_manual_authority_models:

Authority models
================

Every networked decision belongs to exactly one peer, and the whole point of
the addon is that you say which one declaratively instead of writing the check
each time. This page covers the three questions that answer together: what role
this peer is playing, who is allowed to write a given value, and what a peer
without authority does instead.

Roles, and the one that surprises people
----------------------------------------

:ref:`role <class_NetwMultiplayer_property_role>` reports what the local peer is
in the current session.

::

    NONE               no session, so no role yet
    CLIENT             a remote peer holding no authority
    DEDICATED_SERVER   server authority with no local player
    LISTEN_SERVER      server authority held by a peer that is also playing

**Server authority means either server role.** A listen-server host is a
player, so code that tests for
:ref:`CLIENT <class_NetwMultiplayer_constant_CLIENT>` to mean "is a player"
silently excludes the host, and code that tests for
:ref:`DEDICATED_SERVER <class_NetwMultiplayer_constant_DEDICATED_SERVER>` to
mean "has authority" silently excludes the listen server. Very little game code
should test the role at all. Prefer
:ref:`is_host <class_NetwMultiplayer_property_is_host>` for the authority
question and
:ref:`is_local_client <class_NetwMultiplayer_property_is_local_client>` for the
player question. Test the role itself only when the two server flavors genuinely
differ, which in practice means presentation, because a dedicated server has no
local player to show anything to.

Write policy is a property of the value
---------------------------------------

A replicated value declares whose writes are honored through its
:ref:`WritePolicy <enum_NetwMultiplayer_WritePolicy>`, once, in the script that
owns it. The receiver enforces the declaration against its own copy of the
script. Nothing on the wire is ever consulted to decide whether the sender was
allowed, because a forged frame would simply claim it was.

::

    AUTHORITY    only the server. The default, and the safe choice.
    CONTROLLER   only the peer holding the entity's controller, which is how
                 a player drives their own body.
    ANY_PEER     any peer. Trusts every client, so use it only where a
                 forged write cannot matter.

.. tabs::
 .. code-tab:: gdscript GDScript

    func _init() -> void:
        # The server owns where you actually are.
        Netw.configure_property(self, &"position").state()

        # You own what you are trying to do.
        Netw.configure_property(self, &"move_input").input()

The default is the authority policy deliberately. A value you have not thought about is a value the server owns, so
forgetting to declare a policy fails closed.

What each payload is checked against
------------------------------------

The sender check differs by payload kind, and in every case it resolves against
the target's own script.

::

    payload             sender check
    entity call         the method's @rpc mode
    variable, signal    its registered write policy
    any                 the server is always trusted

A frame from an unauthorized sender is dropped and counted, never raised. The
counters are on
:ref:`stats_snapshot() <class_NetwMultiplayer_method_stats_snapshot>` under
:ref:`STAT_DROPS_SYNC_BAD_SENDER <class_NetwMultiplayer_constant_STAT_DROPS_SYNC_BAD_SENDER>`
and
:ref:`STAT_VERDICT_UNAUTHORIZED <class_NetwMultiplayer_constant_STAT_VERDICT_UNAUTHORIZED>`,
so an authority mistake shows up as a rising number rather than as silence.
That is worth checking first when a value "does not replicate": very often it
does replicate and is being refused at the far end.

Control is granted, not taken
-----------------------------

The controller is the peer allowed to write an entity's
:ref:`CONTROLLER <class_NetwMultiplayer_constant_CONTROLLER>` values. It is
server state like everything else.

.. tabs::
 .. code-tab:: gdscript GDScript

    # Server authority decides.
    api.grant_control(entity, peer_id)

    # A client asks. The server handler decides whether to honor it.
    api.request_control(entity)

Asking rather than doing
------------------------

This shape repeats across the whole surface, and recognizing it saves reading
each method's doc. Members are marked one of two ways:

* **Server Only.** The method mutates authoritative state or broadcasts. It
  asserts server authority. Both server roles qualify.
* **Player request.** The method sends a local request to server authority,
  usually over ``rpc_id(1)``. Remote clients and listen-server host players may
  call it, and the receiving server handler decides whether to honor it.

The pairs follow one naming convention, so the request form is always
discoverable from the authoritative one::

    server authority          the matching player request
    ─────────────────────     ──────────────────────────
    grant_control()           request_control()
    kick()                    request_kick()
    scenes.change_to()        scenes.request_change()
    leave()                   request_leave()

A request returns nothing useful by itself. Where the outcome matters, the
request hands back a promise you await, so the client learns the server's
decision rather than assuming it.

Two things that are not authority
---------------------------------

**Interest is not authority.** A layer decides what a peer is allowed to *see*.
It never decides what a peer is allowed to *write*. Hiding an entity from a
client is not a security boundary against that client writing to it, and
admitting an entity does not grant any write. See
:ref:`doc_manual_interest_management`.

**Prediction is not authority.** A predicting client advances its own body
before the server confirms it, and that advance is a claim the server verifies
and may overwrite. The client is not writing state it owns. It is writing input
it owns and displaying a guess about the result. See
:ref:`doc_manual_prediction_model`.

Where to go next
----------------

* :ref:`doc_manual_identity_and_altitudes` for what these calls address.
* :ref:`doc_manual_replication_model` for how a refused frame is counted.
* :ref:`doc_manual_sessions_and_peers` for how a role is assigned in the first
  place.
