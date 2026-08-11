.. _doc_manual_replicated_tables:

Replicated tables
=================

Everything else in this manual replicates a ``Node``. A table replicates rows
that are not nodes at all: two thousand mobs, a field of destructible rocks,
every stroke on a shared canvas. You declare a shape, hand over arrays, and
Networked gets them to the right peers with identity, quantization, MTU
chunking, per-row sequencing, tombstones, and late join already handled.

Reach for this when the thing you are replicating would be a ``Node`` only
because Networked demanded one. A boss is a node. Two thousand mobs driven
straight into ``RenderingServer`` are rows.

Where the line falls
--------------------

::

    YOU own                          NETWORKED owns
    ──────────────────────────────   ─────────────────────────────────────
    storage layout                   route identity, never reused
    the simulation loop              the schema and its hash
    when things are born and die     quantization and bit packing
    iteration, queries, filtering    MTU-sized framing
    composition, relations, tags     freshest-wins sequencing, per row
    change detection                 reliable membership removal
    your ECS, or no ECS at all       late-join snapshot, tombstones

Everything on the right is what a hand roll gets wrong. Everything on the left
is what you would resent us for taking.

Declare a schema
----------------

A schema is declared where the code that uses it lives, usually in
``static var`` initializers, because a declaration reaches no session and needs
none. A session binds a table to every schema that asks for one.

.. tabs::
 .. code-tab:: gdscript GDScript

    class Mobs:
        static var schema := Netw.configure_schema(&"Mob")
        static var pos := schema.vector3(
            &"pos",
            NetwQuantizeFixed.new().step(0.03),
        )
        static var vel := schema.vector3(&"vel")  # unquantized: memcpy path
        static var hp  := schema.u16(&"hp")

Every builder method returns a plain ``int`` column index, which is what makes
the whole thing legal in a static initializer. Each session compiles these
declarations into its own handle, and you fetch it once:

.. tabs::
 .. code-tab:: gdscript GDScript

    var mobs := Netw.of(self).table_find(&"Mob")

Declaration order is wire order, and
:ref:`schema_seal() <class_NetwMultiplayer_method_schema_seal>` is what fixes
it. A shape hash rides every frame, so a peer built against a different
declaration is refused rather than allowed to misread bytes.

A class's ``static var`` initializers run on first access rather than at load,
so touch every table class before the session goes online. A wire id is the
name-sorted position among sealed tables, and two peers that sealed different
sets number them differently.

One schema, three consumers
---------------------------

A schema is a column list and nothing else: no storage, no wire, no session.
That is what lets three different things read the same one instead of three
that drift apart.

.. code-block:: text

    schema        an ordered list of named, typed columns; sealed; shape-hashed

    property set  row-major, property-sourced; values live on nodes; lanes,
                  policy, and the per-tick pump
    table         column-major, array-sourced; one store, N rows; the wire
                  this page describes
    database      typed columns, coercion diagnostics, batch flush

A script's ``configure_property`` declarations compile into a schema too, and
each per-tick set binds the subset it replicates. A column no set binds rides
no lane, which is how a persisted-only property stays off the wire without a
negation mark.

A declaration that only wants the database says so and mints no table at all:

.. tabs::
 .. code-tab:: gdscript GDScript

    static var schema := Netw.configure_schema(&"Ledger").replicated(false)

:ref:`variant() <class_NetwSchema_method_variant>` declares the
self-describing tier a ``String`` or a ``Dictionary`` takes. The property
binding and the database accept it; a table refuses it, because variable width
has no memcpy and no rows-per-frame budget.

Save a table
------------

A table's unit of commit is the whole table, and so is its unit of save. Routes
are session-scoped, so a hydrate mints fresh ones and hands back the pairing
against the save keys you wrote.

.. tabs::
 .. code-tab:: gdscript GDScript

    # server: ids[i] names the row whose route is table_read_routes(mobs)[i]
    var ids := my_stable_ids_for(api.table_read_routes(mobs))
    api.persist_table_flush(mobs, db, &"mobs", ids)

    # server, next session: fresh routes, same values
    var back := await api.persist_table_hydrate(mobs, db, &"mobs")
    rebuild_indexes(back[&"routes"], back[&"ids"])

Two thousand rows are one record, not two thousand files. Values are the
committed storage arrays, which the wire codec never touches, so a column
quantized to three centimetres saves at full precision. A
``COLUMN_ENTITY`` column is skipped with a warning and zero-fills on hydrate,
because a route means nothing in the session that loads it.

The binary record format is lossless for every column type. Turning on
``FileSystemDatabase.use_text_format`` trades that for readability: JSON
numbers are doubles, so a ``COLUMN_F64`` column loses mantissa digits and a
``COLUMN_I64`` past 2\ :sup:`53` saturates.

Claim identity
--------------

A row's identity is a route and nothing else.
:ref:`claim_routes() <class_NetwMultiplayer_method_claim_routes>` mints live
entities with no wrapper and no node.

.. tabs::
 .. code-tab:: gdscript GDScript

    var fresh := api.claim_routes(200)
    routes.append_array(fresh)             # my storage, my order

Claiming is optional. A route you already hold, from another table or from
:ref:`entity_get_route() <class_NetwMultiplayer_method_entity_get_route>`, is
equally writable, and that independence is what makes component tables work.
:ref:`release_routes() <class_NetwMultiplayer_method_release_routes>` is the
death edge: it tombstones the identity everywhere, so a row for it can never
be resurrected by a frame that arrived late.

No per-row signal fires for a wave of rows. Two thousand ``entity_live``
dispatches would be exactly the per-row crossing tables exist to avoid, so
births and deaths arrive as cohort arrays instead.

Publish
-------

.. tabs::
 .. code-tab:: gdscript GDScript

    func _physics_process(dt: float) -> void:
        for i in pos.size():
            pos[i] += vel[i] * dt              # my loop, my rules
        api.table_write_routes(mobs, routes)
        api.table_write_column(mobs, Mobs.pos, pos)
        api.table_write_column(mobs, Mobs.vel, vel)
        api.table_write_column(mobs, Mobs.hp, hp)
        api.table_commit(mobs)                 # snapshots; my arrays stay mine

**Not committing is the cadence.** There is no interval to declare and no
change detection to configure, because you already know when your data changed.
A table nothing commits sends nothing, which is what makes a field of rocks
silent at rest.

**The commit copies.** GDScript packed arrays are shared references rather than
copy-on-write, so a commit that did not copy could be torn by the next line of
your own loop. After
:ref:`table_commit() <class_NetwMultiplayer_method_table_commit>` returns, your
arrays are yours again, unconditionally.

Every column must be written and every length must equal
``routes.size() * stride``, so a half table can never reach the wire. The wire
itself happens at the tick boundary, so several commits inside one tick
collapse to the last, and a session with no configured clock carries no table
traffic at all.

Consume
-------

**A read is a live view.** The array you get back is the receive store itself,
which is what makes reading a two thousand row column free. Treat it as
read-only, and ``duplicate()`` it to keep history. That escape is also the
interpolation pattern, with no new surface:

.. tabs::
 .. code-tab:: gdscript GDScript

    func _on_table_received(table: RID, _tick: int) -> void:
        if table != mobs:
            return
        prev_pos = next_pos                      # rebind: free
        next_pos = api.table_read_column(mobs, Mobs.pos).duplicate()
        for route in api.table_read_deaths(mobs):
            free_visual(route)

    func _process(_dt: float) -> void:
        var t := blend_factor()                  # my clock, my rules
        for i in mini(prev_pos.size(), next_pos.size()):
            write_instance_xform(i, prev_pos[i].lerp(next_pos[i], t))

:ref:`table_received <class_NetwMultiplayer_signal_table_received>` fires once
per table per applied wave, so a commit that needed twenty two frames still
emits once. Polling
:ref:`table_get_tick() <class_NetwMultiplayer_method_table_get_tick>` in your
own loop is equally first-class.

**Births and deaths are derived, not decoded.** A birth is a route this table
held no row for, and a death is a row a removal took away. Deriving them
locally is what makes them exact under loss: a dropped datagram delays a row's
data and can never lose a cohort.

**Server and client read the same way.** On the authority, including a
listen-server host, reads return the committed snapshot and
``table_received`` fires locally at the tick boundary.

Composition falls out of "more than one table"
----------------------------------------------

There is no limit on how many tables key on the same routes, and that one fact
replaces components, tags, and relations.

::

    an entity            = a route
    a component          = a table keyed by that route, holding only the rows
                           that have it
    a tag                = a table with zero columns; membership IS the value
    gaining a component  = the route appears in that table's births
    losing it            = it appears in its deaths
    a relation / edge    = a table with two ENTITY columns

A hundred burning mobs cost a hundred rows, not a masked section across two
thousand. Joining back is one crossing, never one per row:

.. tabs::
 .. code-tab:: gdscript GDScript

    var rows := api.table_get_rows(mobs, burning_routes)
    for i in burning_routes.size():
        if rows[i] >= 0:
            hp[rows[i]] -= burning_dps[i] * dt

Fixed width, and the escape hatch
---------------------------------

Every :ref:`ColumnType <enum_NetwMultiplayer_ColumnType>` is fixed width. That
is the premise rather than a limitation: it is what makes a column's bytes a
memcpy, makes rows-per-frame computable against the MTU, and lets the native
port share a pointer instead of boxing a row.

::

    fits                                       does not fit
    ────────────────────────────────────────   ──────────────────────────────
    scalars, vectors, colors, quaternions      strings and variable blobs
    entity references (a route)                arbitrary Variant / Dictionary
    fixed-capacity arrays (stride N)           nested structs, Resources

So the rule is one sentence: **fixed-width per-row state goes in a table,
variable-length payloads go on a**
:ref:`channel <class_Netw_method_channel>`\ **, and the route is what makes the
two halves refer to the same thing.**

.. tabs::
 .. code-tab:: gdscript GDScript

    var geom := Netw.channel(self, 42)
    geom.send(peer, encode_points(route, points), true)   # you own the encoding

A fixed-capacity array is a ``stride`` rather than a second type family. Eight
ability cooldowns is one ``COLUMN_F32`` column of stride eight.

Reliable or not
---------------

::

    default     unreliable. Losing a frame costs those rows one tick of
                freshness, and the next commit heals it.

    reliable    for a table that changes rarely, because it has no next
                commit to heal with. Set TABLE_PARAM_RELIABLE, or call
                .reliable() on the builder.

Membership subtraction, tombstones, and the late-join snapshot always ride the
reliable lane, whichever kind the table is, because they must be exact and they
are rare.

What is not here yet
--------------------

**Per-row interest.** A table publishes to every peer that can see the session.
The interest engine keys on entity object identity rather than on a route, so
per-row visibility waits on that engine re-keying. When it lands, visibility
policy stays yours: distance, rooms, and teams are decisions a game makes, and
Networked ships the partitioning mechanism rather than the policy.

**The display pump.** Rows deliberately do not enter it. That pump is one
runtime, one history, and one virtual write per row per frame, which is the
per-row crossing this whole surface exists to avoid. Batch display is your own
loop over columns into ``RenderingServer``, which
``examples/swarm/swarm_view.gd`` shows in about thirty lines.

A worked example
----------------

``examples/swarm/`` is the whole thing end to end: a server simulating two
thousand mobs in plain arrays, a client rendering them through one
``multimesh_set_buffer`` per frame, a ``Burning`` component table joining back
in one call, and a measurement of what the GDScript implementation actually
costs at that scale.
