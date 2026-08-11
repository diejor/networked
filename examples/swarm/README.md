# Swarm Example

Two thousand mobs, no nodes, no `SceneTree` involvement per row. This is the
example for the case the rest of the addon does not serve: a large body of
simple things whose state changes every tick and which never needs to be a
`Node`.

## What it shows

- **Nodeless identity.** `api.claim_routes(n)` mints `n` live entities with no
  wrapper and no node. A row *is* a route, and `api.rid_from_route(route)`
  turns one back into an ordinary handle when something needs it to be.
- **Plain arrays.** `swarm_server.gd` simulates in `PackedVector3Array` and
  friends. Networked sees those arrays exactly twice per tick, at the two
  `table_commit` calls.
- **Servers-direct rendering.** `swarm_view.gd` writes one
  `RenderingServer.multimesh_set_buffer` for the whole swarm. Rows deliberately
  do not enter the display pump, because that pump is one virtual write per row
  per frame.
- **A component table.** `Burning` is a second table keyed on the same routes,
  holding only the rows that have the effect. A hundred burning mobs cost a
  hundred rows, not a masked section across two thousand. The join back is one
  `table_get_rows` call.
- **Interpolation as a caller pattern.** A read returns the receive store by
  reference, so keeping last wave's column is a variable rebind and the whole
  history cost is one `duplicate()` per wave.

## The shape

```
swarm_tables.gd   the schema, in static var initializers, no session
swarm_server.gd   server: simulate columns, write, commit
swarm_view.gd     client: two-wave blend into one MultiMesh buffer
tests/            the harness suite, and the N=2000 measurement
```

Declaration happens in `static var` initializers, which run on first access
rather than at load, so both scripts call `SwarmTables.declare_all()` in
`_ready`. A wire id is the name-sorted position among sealed tables, so both
peers must have declared the same set before either sends a frame.

## Measured

`test_measures_the_two_thousand_row_ceiling` prints the honest GDScript-era
cost rather than asserting a threshold. On the development machine, a
2,000-row tick of a quantized position, a raw velocity, and health is roughly
3.5 ms to encode, 15 ms to decode, 48 frames and 44 KB per tick.

The bandwidth is final, because it is wire. The microseconds are not: they are
the per-element quantize loop and the per-element rebuild, which are what the
native port replaces with a memcpy.

## Running it

```sh
godot --path . examples/swarm/main.tscn
```

Host from the browser panel, then join from a second instance. The mobs are
server-authored, so the client window renders a swarm it owns no node for.
