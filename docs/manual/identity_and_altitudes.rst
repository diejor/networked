.. _doc_manual_identity_and_altitudes:

Identity and altitudes
======================

One replicated thing carries four names at once, and each name exists because
a different part of the system cannot use the others. A node is what your game
code holds. An entity RID is what the server addresses. A route is what the
wire carries. A handle is what a script configures. Confusing them is the most
common source of "it works on the host and not on the client", so this page
names each one and says exactly where it is valid.

The four names
--------------

::

    Node          your scene's own object. Valid only on a peer that has
                  actually spawned it. Never sent.

    NetwEntity    the component on the node's root that makes the node
                  addressable at all. Owns the per-entity handles.

    RID           the server's local handle for the entity. Cheap, stable
                  for the entity's life, and meaningless on another peer.

    route         a session-monotonic integer naming the entity on the
                  wire. The same number on every peer. Never reused.

A node without a :ref:`NetwEntity <class_NetwEntity>` on its root is not
addressable, which is why every transport that carries entity traffic requires
one. The RID and the route are deliberately different numbers: the RID is a
local slot the server can hand out and free without agreement, while the route
must mean the same thing on two machines that have never compared notes.

Translating between them is the job of four verbs on
:ref:`NetwMultiplayer <class_NetwMultiplayer>`:

::

    node  -> RID     rid_of(node)
    RID   -> node    entity_get_node(entity)
    RID   -> route   entity_get_route(entity)
    route -> RID     rid_from_route(route)

.. tabs::
 .. code-tab:: gdscript GDScript

    var api := NetwMultiplayer.of(self)
    var entity := api.rid_of(self)          # this node's server handle
    var route := api.entity_get_route(entity)   # what the wire calls it

Only the server allocates a route, through
:ref:`entity_allocate_route() <class_NetwMultiplayer_method_entity_allocate_route>`.
The route rides the SPAWN frame header, so a peer learns it inside the same
reliable message that creates the node. By the time
:ref:`rid_from_route() <class_NetwMultiplayer_method_rid_from_route>` can
resolve a route, the node it names already exists.

::

    server                                 client
    entity_allocate_route(entity) -> 7
    spawn packet { ..., route: 7 }  ────▶  node enters tree
                                           route bound automatically
                                           rid_from_route(7) resolves

An entity that never gets a route never rides that channel. Bind one on every
peer with
:ref:`entity_bind_route() <class_NetwMultiplayer_method_entity_bind_route>`, or
leave the entity unroutable on purpose.

Existence is a state, not a question
------------------------------------

A spawn packet and an unreliable state packet travel on uncoupled streams, so
either can arrive first. A state frame for an entity the receiver has not
spawned yet is normal traffic during that window, not an error. Networked turns
the window into a queryable condition instead of raising on it.

:ref:`route_get_state() <class_NetwMultiplayer_method_route_get_state>` and
:ref:`entity_get_state() <class_NetwMultiplayer_method_entity_get_state>` answer with an
:ref:`EntityState <class_NetwMultiplayer_constant_UNKNOWN>`, and a route only
ever moves forward through it::

    UNKNOWN ──spawn──▶ LIVE ──despawn linger──▶ LINGERING ──window──▶ DEAD
                        │
                        └───────plain despawn──────────────────────▶ DEAD

:ref:`DEAD <class_NetwMultiplayer_constant_DEAD>` routes stay tombstoned for the
whole session. That is what lets a late packet resolve to "known but dead" and
be dropped, instead of being mistaken for a packet that arrived early.
:ref:`LINGERING <class_NetwMultiplayer_constant_LINGERING>` keeps the route
resolvable while a despawn winds the node down, which is what lets a late
lag-compensation query still name a dying entity. A reparent is not a despawn
and never kills a route.

The one sanctioned revival is a spawn frame arriving after the despawn that
tombstoned the route, which is an interest re-admission. Spawn and despawn share
one reliable ordered channel, so that ordering can never be stale.

Waiting instead of retrying
---------------------------

Reliable work that must not be lost across the spawn window uses
:ref:`when_live() <class_NetwMultiplayer_method_when_live>` rather than a
hand-rolled retry loop. The callback runs as soon as the route resolves, and is
swept if the window expires.

.. tabs::
 .. code-tab:: gdscript GDScript

    api.when_live(route, func() -> void:
        apply_event(api.entity_get_node(api.rid_from_route(route)))
    )

The altitudes
-------------

The same capability is reachable from three heights. They are not alternatives
to pick between once. Each is the natural altitude for a different kind of
statement, and a normal game uses all three.

::

    Netw                 declaration altitude. Says what a script's values
                         MEAN, once, at _init time. Compiled and shared by
                         every instance of that script.

    NetwEntity handles   instance altitude. Says what is exceptional about
                         THIS entity: its display root, its prediction
                         archetype, its interest attribution.

    NetwMultiplayer      server altitude. Flat verbs over RIDs. What the
                         engine itself calls, and what an override
                         replaces.

.. tabs::
 .. code-tab:: gdscript GDScript

    func _init() -> void:
        # Declaration: true of every Player, forever.
        Netw.configure_property(self, &"position").state().epsilon(0.05)

    func _ready() -> void:
        # Instance: true of this one.
        var entity := NetwEntity.ensure(self)
        entity.interpolation.visual_root = NodePath("Sprite")

        # Server: the flat verb the two above eventually reach.
        var api := NetwMultiplayer.of(self)
        api.layer_add_entity(api.layer_find(&"arena"), api.rid_of(self))

The band rule
-------------

:ref:`NetwMultiplayer <class_NetwMultiplayer>` carries two bands of members, and
one mechanical rule tells them apart.

::

    takes an RID   ->  server verb. A function, never a property. It acts
                       on one addressed object and says nothing about the
                       session as a whole.

    takes no RID   ->  session verb. May be a property, because there is
                       exactly one session and its facts are singular.

So :ref:`tick <class_NetwClockHandle_property_tick>` and
:ref:`role <class_NetwMultiplayer_property_role>` are properties, while
:ref:`display_set_param() <class_NetwMultiplayer_method_display_set_param>` and
:ref:`interest_admits() <class_NetwMultiplayer_method_interest_admits>` are
functions taking the entity they concern. The rule is worth knowing because it
predicts where a member lives before you look for it.

Where to go next
----------------

* :ref:`doc_manual_authority_models` for who is allowed to make each of these
  calls.
* :ref:`doc_manual_replication_model` for what the route actually carries.
* :ref:`doc_manual_extending` for replacing a server verb with your own.
