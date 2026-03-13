# SceneNodePath for Godot 4

**SceneNodePath** is a powerful custom Resource and Editor UI plugin for Godot 4. It solves a common architectural problem: safely referencing and instantiating a specific Node inside an *external* PackedScene.

Instead of exporting a `PackedScene`, instantiating it in code, and blindly using `get_node("Path/To/Node")` (which breaks if the scene changes), `SceneNodePath` gives you a dedicated Inspector UI to browse the external scene, pick the exact node you want, and safely load it at runtime.

## Features
* **Custom Node Picker UI:** Browse a filterable tree of the remote scene directly from the Inspector.
* **UID Support:** Paths are automatically saved as `uid://` strings. If you move your scene files in the FileSystem dock, your references won't break.
* **Type-Safe Filtering:** Restrict the Inspector to only allow selecting specific node types (e.g., only `Area3D` or your custom `class_name`).
* **Editor Context Menus:** Native right-click integration in the Scene Tree dock allows you to instantly copy any node as a `SceneNodePath` string for your code.
* **Bulletproof API:** A heavily optimized, minimal runtime API designed to instantiate the scene and hand you the exact node you requested.

---

## Installation

1. Download the repository and extract the `addons/scene_node_path` folder.
2. Move the `scene_node_path` folder into your Godot project's `res://addons/` directory.
3. Open your project, go to **Project -> Project Settings -> Plugins**.
4. Check the **Enable** box next to "SceneNodePath".

---

## Quick Start

Simply export the resource in your script. The Inspector will present an "Assign Scene" button.

```gdscript
@export var spawn_point: SceneNodePath
