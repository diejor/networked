## Attribution

Ported from [Starter Kit Racing](https://github.com/KenneyNL/Starter-Kit-Racing)
by Kenney, MIT licensed. `LICENSE` in this directory is that project's licence,
and the models, sounds and vehicle arithmetic are its work. The 2D sprites, 3D
models and sound effects are CC0, the skid sound effect is by
[Landeplage](https://github.com/Landeplage) and is also CC0.

`vehicle.gd` is mostly unchanged. The port adds six
`Netw.configure_property` broadcast declarations, an interest join, and a
`simulation.bodies` declaration. The upstream `_physics_process` runs as
`_network_tick`, which only runs on the peer that controls the car.
