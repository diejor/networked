## Cube Playground

Each player rolls a cube through a field of 900 small cubes. A small cube
takes the colour of the player that has authority over it, and fades back to
grey once it rests.

```text
arrows / WASD   roll
Space           hover, pushing nearby cubes away
Z               attract nearby cubes and hold them against other players
```

## Attribution

Ported from [NetworkedPhysics](https://github.com/valverl/NetworkedPhysics)
by Luis Valverde, MIT licensed. `LICENSE` in this directory is that project's
licence, and the player controller, world layout, palette and tuning values
are its work.

Both follow Glenn Fiedler's
[Networked Physics](https://gafferongames.com/categories/networked-physics/)
series. This port uses its state synchronization model. Each player simulates
the cubes it touches and the server decides who has authority over each cube.
