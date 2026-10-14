# Starter Kit FPS

A multiplayer first person shooter.

## Attribution

Ported from [Starter Kit FPS](https://github.com/KenneyNL/Starter-Kit-FPS) by
Kenney, MIT licensed. `LICENSE` in this directory is that project's licence,
and the scenes, scripts, sprites, 3D models and sound effects are its work. The
assets are [CC0 licensed](https://creativecommons.org/publicdomain/zero/1.0/).

Each player moves its own body and every peer receives its `position` and
`rotation`. A shot that hits is sent to the server, which casts the ray again
and applies the damage on every peer. Enemies run on the server.

### Controls

| Key | Command |
| --- | --- |
| <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> | Movement |
| <kbd>Spacebar</kbd> | Jump |
| <kbd>Left mouse button</kbd> | Shoot |
| <kbd>E</kbd> | Switch weapon |
