# LOGL: NetLog Configuration Language

LOGL is a concise, "Rust-style" string configuration language used by the `networked` addon's `NetLog` system in Godot. It allows developers to quickly enable, disable, and layer logging output for different parts of the codebase without needing to create `.tres` resource files for every scenario.

## Syntax

A LOGL string is a comma-separated list of directives.

```text
[global_level], [module.path=level], [module.path=level]...
```

- **Global Level**: A single word indicating the default log level for the entire system (e.g., `info`, `trace`, `error`). This is optional. If omitted, the system falls back to `INHERIT` and relies on the underlying base profile.
- **Module Override**: A key-value pair separated by an equals sign `=`. The key is the dot-separated module path, and the value is the target log level.
- **Levels**: Available levels are `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `NONE`, and `INHERIT`. Level names are case-insensitive.

### Examples

```text
# Set everything to INFO
info

# Set the global level to WARN, but enable TRACE for the network session module
warn,core.network_session=trace

# Disable all output from the lobby module, while leaving the rest of the system as-is
core.lobby=none

# Complex layering: Global DEBUG, but silence components, and TRACE the backend
debug,components=none,core.backends=trace
```

## How It Works

### Implicit Inheritance
The module path relies on a dot-separated hierarchy (e.g., `core.lobby.manager` inherits from `core.lobby`, which inherits from `core`).
When you set an override like `core=trace`, it implicitly applies to all submodules under `core` (like `core.lobby` and `core.network_session`) unless explicitly overridden further down.

### Cascading Stack
The `NetLog` system uses a stack. The base is always your active `NetLogSettings` `.tres` profile selected in the Godot Editor.
When you push a new LOGL string at runtime, it goes on top of the stack.

When checking the log level for a specific module:
1. It queries the most recently pushed setting.
2. If that setting explicitly configures the module, it uses that level.
3. If the pushed setting specifies a `global_level`, it uses that level.
4. If neither matches (or if they are set to `INHERIT`), it falls back to the *next* setting down the stack, eventually reaching the base profile.

## API Usage

You can parse a LOGL string into a `NetLogSettings` resource, or dynamically push it at runtime.

```gdscript
# Push a configuration temporarily (e.g., in a test setup or via command-line args)
NetLog.push_setting_str("info,core.network=trace,components=none")

# ... run your code ...

# Pop the configuration when done
NetLog.pop_settings()
```

You can also parse and serialize strings manually:
```gdscript
# Parse LOGL to a Resource
var settings: NetLogSettings = NetLog.parse_logl("core=debug")

# Serialize a Resource to LOGL
var string_representation: String = NetLog.to_logl(settings)
```