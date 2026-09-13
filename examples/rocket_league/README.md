## Attribution

Ported from [godot-rocket-league](https://github.com/albertok/godot-rocket-league)
by Alberto Klocker, MIT licensed. `LICENSE` in this directory is that project's
licence, and the models, materials and vehicle arithmetic are its work.

The port replaces the original's netfox rollback stack with this framework:
`RollbackSynchronizer`, `StateSynchronizer`, `TickInterpolator` and
`PhysicsDriver3D` are gone, replaced by `Netw.configure_property` declarations
and the framework's own tick drive.
