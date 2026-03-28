using Godot;

/// <summary>
/// A resource that stores a reference to a specific <see cref="Node"/> within an external <see cref="PackedScene"/>.
/// <br/><br/>
/// Allows for cross-scene node references that survive file movement (via UIDs).
/// Provides methods to safely instantiate the <see cref="ScenePath"/> and retrieve the <see cref="Node"/> at <see cref="NodePath"/>.
/// <br/><br/>
/// You can assign a path via the Inspector, or create one dynamically in code using its string representation.
/// <code>
/// [Export] public SceneNodePathCS PortalDestination;
/// 
/// public void SetupLevel() 
/// {
///     // 1. Instantiates the scene. Returns a Result object with handles to the root and target.
///     var result = PortalDestination.Instantiate();
///     if (result != null)
///     {
///         AddChild(result.Root);
///         Node portal = result.Node;
///     }
/// 
///     // 2. Pulls the node out and deletes the rest of the scene automatically.
///     Node isolatedBoss = new SceneNodePathCS("uid://b4x8...::%Boss").Extract();
///     AddChild(isolatedBoss);
/// }
/// </code>
/// </summary>
[Tool]
[GlobalClass]
public partial class SceneNodePathCS : Resource
{
	/// <summary>
	/// The path to the scene file. Stored as a UID when possible to prevent broken references.
	/// </summary>
	[Export(PropertyHint.File, "*.tscn")]
	public string ScenePath { get; set; } = "";

	/// <summary>
	/// The internal path to the target node within the referenced scene.
	/// </summary>
	[Export]
	public string NodePath { get; set; } = "";

	/// <summary>
	/// Internal variable updated by the Inspector plugin to store validation errors.
	/// Useful for forwarding warnings to a Node's _get_configuration_warnings().
	/// Kept public to allow duck-typed property injection from GDScript UI.
	/// </summary>
	public string _editor_property_warnings { get; set; } = "";

	private readonly StateCache _cache = new StateCache();
	
	public SceneNodePathCS() { }
	
	public SceneNodePathCS(string formattedPath)
	{
		if (!string.IsNullOrEmpty(formattedPath))
			Parse(formattedPath);
	}

	/// <summary>
	/// Parses a formatted string (e.g., "scene_path::node_path") and assigns the values.
	/// </summary>
	public void Parse(string formattedPath)
	{
		System.Diagnostics.Debug.Assert(formattedPath.Contains("::"), "SceneNodePath: Invalid string format.");
		string[] parts = formattedPath.Split("::", 2, System.StringSplitOptions.None);

		NodePath = parts[1];
		string rawScene = parts[0];

		if (rawScene.StartsWith("res://"))
		{
			if (ResourceLoader.Exists(rawScene))
			{
				long uid = ResourceLoader.GetResourceUid(rawScene);
				ScenePath = uid != ResourceUid.InvalidId ? ResourceUid.IdToText(uid) : rawScene;
			}
			else
			{
				ScenePath = rawScene;
			}
		}
		else if (rawScene.StartsWith("uid://"))
		{
			ScenePath = rawScene;
		}
		else
		{
			ScenePath = rawScene;
		}
	}

	private static Node GetOrphanRoot(Node node)
	{
		if (node == null) return null;
		Node current = node;
		while (current.GetParent() != null)
		{
			current = current.GetParent();
		}
		return current;
	}

	/// <summary>
	/// Returns <c>true</c> if the <see cref="ScenePath"/> and <see cref="NodePath"/> are not empty
	/// and the scene file exists on disk.
	/// <br/><br/>
	/// <b>Note:</b> This is a surface-level check. It does <b>not</b> verify if the <see cref="NodePath"/>
	/// actually exists inside the scene file. Use <see cref="StateInspector.IsValid"/> for a more robust validation.
	/// </summary>
	public bool IsValid()
	{
		if (string.IsNullOrEmpty(ScenePath) || string.IsNullOrEmpty(NodePath))
			return false;
		string realPath = SafeResolvePath(ScenePath);
		return !string.IsNullOrEmpty(realPath) && ResourceLoader.Exists(realPath);
	}

	/// <summary>
	/// Instantiates the entire scene referenced by <see cref="ScenePath"/> and returns a 
	/// <see cref="Result"/> containing the scene root and the specific target <see cref="Node"/>.
	/// <br/><br/><b>Note:</b> This method does <i>not</i> add the nodes to the SceneTree. 
	/// You are responsible for adding <see cref="Result.Root"/> to the tree and managing its lifecycle.
	/// </summary>
	public Result Instantiate() => Instantiate(0);
	public Result Instantiate(PackedScene.GenEditState editState)
	{
		Node targetNode = InstantiateAndGet(editState);
		if (targetNode == null) return null;

		return new Result(GetOrphanRoot(targetNode), targetNode);
	}

	/// <summary>
	/// Identical to <see cref="Instantiate"/>, but safely returns <c>null</c> on 
	/// failure without asserting.
	/// </summary>
	public Result InstantiateOrNull() => InstantiateOrNull(0);
	public Result InstantiateOrNull(PackedScene.GenEditState editState)
	{
		Node targetNode = InstantiateAndGetOrNull(editState);
		if (targetNode == null) return null;

		return new Result(GetOrphanRoot(targetNode), targetNode);
	}

	/// <summary>
	/// A container for the results of a <see cref="SceneNodePathCS.Instantiate"/> call.
	/// </summary>
	public partial class Result : RefCounted
	{
		/// <summary>
		/// The root node of the newly instantiated scene hierarchy.
		/// Use this to add the scene to the SceneTree or to free it later.
		/// </summary>
		public Node Root { get; set; }

		/// <summary>
		/// The specific node referenced by the <see cref="SceneNodePathCS.NodePath"/>.
		/// </summary>
		public Node Node { get; set; }

		public Result() { } // Required for RefCounted

		public Result(Node root, Node node)
		{
			Root = root;
			Node = node;
		}
	}

	/// <summary>
	/// Instantiates the entire scene referenced by <see cref="ScenePath"/>, isolates the target <see cref="Node"/> 
	/// at <see cref="NodePath"/>, and <see cref="Node.QueueFree"/>s the rest of the scene.
	/// <br/><br/>
	/// <b>Warning:</b> The extracted node is surgically removed from its scene tree via <see cref="Node.RemoveChild"/>. 
	/// It loses its original siblings and parent context.
	/// </summary>
	public Node Extract() => Extract(0);
	public Node Extract(PackedScene.GenEditState editState)
	{
		return PerformExtraction(InstantiateAndGet(editState));
	}

	/// <summary>
	/// Identical to <see cref="Extract"/>, but safely returns <c>null</c> on failure.
	/// </summary>
	public Node ExtractOrNull() => ExtractOrNull(0);
	public Node ExtractOrNull(PackedScene.GenEditState editState)
	{
		Node target = InstantiateAndGetOrNull(editState);
		return target != null ? PerformExtraction(target) : null;
	}

	/// <summary>
	/// Returns the absolute file path and node path combined (e.g., <c>"res://scene.tscn::Node"</c>).
	/// </summary>
	public string AsPath()
	{
		string realScene = ScenePath;
		if (realScene.StartsWith("uid://"))
		{
			long id = ResourceUid.TextToId(realScene);
			if (ResourceUid.HasId(id))
			{
				realScene = ResourceUid.GetIdPath(id);
			}
		}
		return $"{realScene}::{NodePath}";
	}

	/// <summary>
	/// Returns the UID path and node path combined (e.g., <c>"uid://...::Node"</c>).
	/// </summary>
	public string AsUid()
	{
		string uidScene = ScenePath;
		if (!uidScene.StartsWith("uid://") && ResourceLoader.Exists(uidScene))
		{
			long id = ResourceLoader.GetResourceUid(uidScene);
			if (id != ResourceUid.InvalidId)
			{
				uidScene = ResourceUid.IdToText(id);
			}
		}
		return $"{uidScene}::{NodePath}";
	}

	/// <summary>
	/// Returns the file name of the scene referenced by <see cref="ScenePath"/>, excluding the extension.
	/// <br/><br/>
	/// For example, if <see cref="ScenePath"/> is <c>"res://maps/dungeon_01.tscn"</c>, this returns <c>"dungeon_01"</c>.
	/// </summary>
	public string GetSceneName()
	{
		if (string.IsNullOrEmpty(ScenePath)) return "";

		string realPath = ScenePath;
		if (realPath.StartsWith("uid://"))
		{
			long id = ResourceUid.TextToId(realPath);
			if (ResourceUid.HasId(id))
			{
				realPath = ResourceUid.GetIdPath(id);
			}
		}

		return realPath.GetFile().GetBaseName();
	}

	/// <summary>
	/// Returns the name of the node (or an ancestor) referenced by <see cref="NodePath"/>.
	/// <br/><br/>
	/// The <paramref name="parentOffset"/> determines which segment of the <see cref="Godot.NodePath"/> to return:
	/// <br/>- <c>0</c>: The target node's name.
	/// <br/>- <c>1</c>: The name of the target node's parent.
	/// <br/>- <c>2</c>: The name of the target node's grandparent, and so on.
	/// </summary>
	public string GetNodeName(int parentOffset = 0)
	{
		if (string.IsNullOrEmpty(NodePath) || parentOffset < 0) return "";

		NodePath path = new NodePath(NodePath);
		int nameCount = path.GetNameCount();

		if (nameCount == 0 || parentOffset >= nameCount) return "";

		// Invert the index
		int targetIdx = (nameCount - 1) - parentOffset;

		string targetName = path.GetName(targetIdx).ToString();

		// Clean up the name if it's a Scene Unique Node
		if (targetName.StartsWith("%"))
		{
			targetName = targetName.TrimPrefix("%");
		}

		return targetName;
	}

	public override string ToString()
	{
		if (string.IsNullOrEmpty(ScenePath) && string.IsNullOrEmpty(NodePath))
			return "<SceneNodePath: Empty>";
		return $"<SceneNodePath: {AsPath()}>";
	}

	private Node InstantiateAndGet() => InstantiateAndGet(0);
	private Node InstantiateAndGet(PackedScene.GenEditState editState)
	{
		System.Diagnostics.Debug.Assert(!string.IsNullOrEmpty(ScenePath), "SceneNodePath: scene_path is empty.");
		System.Diagnostics.Debug.Assert(!string.IsNullOrEmpty(NodePath), "SceneNodePath: node_path is empty.");

		string realPath = SafeResolvePath(ScenePath);
		System.Diagnostics.Debug.Assert(!string.IsNullOrEmpty(realPath), "SceneNodePath: Invalid UID or scene path.");

		var packedScene = ResourceLoader.Load<PackedScene>(realPath);
		System.Diagnostics.Debug.Assert(packedScene != null, $"SceneNodePath: Failed to load scene at {realPath}");

		Node sceneInstance = packedScene.Instantiate(editState);
		NodePath targetPath = new NodePath(NodePath);
		Node targetNode;

		if (targetPath == new NodePath(".") || NodePath == sceneInstance.Name.ToString())
		{
			targetNode = sceneInstance;
		}
		else
		{
			targetNode = sceneInstance.GetNodeOrNull(targetPath);
		}

		if (targetNode == null)
		{
			sceneInstance.Free();
			System.Diagnostics.Debug.Assert(false, $"SceneNodePath: Could not find node at {NodePath}");
			return null;
		}

		return targetNode;
	}

	private Node InstantiateAndGetOrNull() => InstantiateAndGetOrNull(0);
	private Node InstantiateAndGetOrNull(PackedScene.GenEditState editState)
	{
		if (!IsValid()) return null;

		string realPath = SafeResolvePath(ScenePath);
		if (string.IsNullOrEmpty(realPath)) return null;

		var packedScene = ResourceLoader.Load<PackedScene>(realPath);
		if (packedScene == null) return null;

		Node sceneInstance = packedScene.Instantiate(editState);
		NodePath targetPath = new NodePath(NodePath);
		Node targetNode;

		if (targetPath == new NodePath(".") || NodePath == sceneInstance.Name.ToString())
		{
			targetNode = sceneInstance;
		}
		else
		{
			targetNode = sceneInstance.GetNodeOrNull(targetPath);
		}

		if (targetNode == null)
		{
			sceneInstance.Free();
			return null;
		}

		return targetNode;
	}

	private Node PerformExtraction(Node target)
	{
		if (target == null) return null;

		Node root = GetOrphanRoot(target);

		if (root != target)
		{
			target.GetParent().RemoveChild(target);
			root.QueueFree();
			ClearOwnership(target);
		}

		return target;
	}

	private void ClearOwnership(Node node)
	{
		node.Owner = null;
		foreach (Node child in node.GetChildren(true))
		{
			ClearOwnership(child);
		}
	}

	private static string SafeResolvePath(string path)
	{
		if (string.IsNullOrEmpty(path))
			return "";
		if (path.StartsWith("uid://"))
		{
			long id = ResourceUid.TextToId(path);
			if (ResourceUid.HasId(id))
			{
				return ResourceUid.GetIdPath(id);
			}
			return ""; // Silently fail if the UID is missing
		}
		return path;
	}

	/// <summary>
	/// Returns a <see cref="StateInspector"/> object, allowing you to read the target node's data 
	/// directly from the <see cref="SceneState"/> without instantiating the scene into memory.
	/// <br/><br/>
	/// This performs a recursive deep-search. It can perfectly read data from nodes buried 
	/// inside instanced sub-scenes, accurately resolving property overrides.
	/// <code>
	/// StateInspector inspector = portalPath.Peek();
	/// if (inspector.IsValid()) {
	///     GD.Print("Target is a: ", inspector.GetNodeType());
	/// }
	/// </code>
	/// </summary>
	public StateInspector Peek()
	{
		SceneState rootState = _cache.GetValidState(ScenePath);
		var (state, idx) = _cache.ResolveDeepNode(rootState, NodePath);

		return new StateInspector(state, idx);
	}

	/// <summary>
	/// A transient data object that provides read-only access to a specific node's <see cref="SceneState"/>.
	/// <br/>
	/// <b>Note:</b> This object is intended to be created via <see cref="SceneNodePath.Peek"/> and 
	/// should not be instantiated directly.
	/// <code>
	/// // 'Hitbox' is inside 'player.tscn', which is instanced inside 'level.tscn'.
	/// var path = new SceneNodePathCS("res://level.tscn::Player/Hitbox");
	/// 
	/// StateInspector inspector = path.Peek();
	/// if (inspector.IsValid()) {
	///     GD.Print("Found deep node type: ", inspector.GetNodeType());
	/// }
	/// </code>
	/// </summary>
	public partial class StateInspector : RefCounted
	{
		private readonly SceneState _state;
		private readonly int _idx;

		public StateInspector() { } // Required for RefCounted

		public StateInspector(SceneState state, int idx)
		{
			_state = state;
			_idx = idx;
		}

		/// <summary>
		/// Returns <c>true</c> if the target node was found anywhere within the scene file 
		/// or its nested sub-scenes.
		/// </summary>
		public bool IsValid() => _state != null && _idx != -1;

		/// <summary>
		/// Returns the class type of the target node (e.g., <c>"Area3D"</c>).
		/// </summary>
		public StringName GetNodeType() => IsValid() ? _state.GetNodeType(_idx) : new StringName("");

		/// <summary>
		/// Returns a dictionary of all exported or overridden property values on the target node.
		/// </summary>
		public Godot.Collections.Dictionary GetProperties()
		{
			var props = new Godot.Collections.Dictionary();
			if (IsValid())
			{
				for (int p = 0; p < _state.GetNodePropertyCount(_idx); p++)
				{
					props[_state.GetNodePropertyName(_idx, p)] = _state.GetNodePropertyValue(_idx, p);
				}
			}
			return props;
		}

		/// <summary>
		/// Returns a specific property value from the scene file, or <paramref name="defaultValue"/> if not found.
		/// </summary>
		public Variant GetProperty(StringName propName, Variant defaultValue = default)
		{
			if (IsValid())
			{
				for (int p = 0; p < _state.GetNodePropertyCount(_idx); p++)
				{
					if (_state.GetNodePropertyName(_idx, p) == propName)
					{
						return _state.GetNodePropertyValue(_idx, p);
					}
				}
			}
			return defaultValue;
		}

		/// <summary>
		/// Returns an array of the groups assigned to the node within the scene file.
		/// </summary>
		public string[] GetGroups() => IsValid() ? _state.GetNodeGroups(_idx) : System.Array.Empty<string>();

		/// <summary>
		/// Returns the <see cref="PackedScene"/> for the node if it is a scene instance, or <c>null</c> if not.
		/// </summary>
		public PackedScene GetNodeInstance() => IsValid() ? _state.GetNodeInstance(_idx) : null;

		/// <summary>
		/// Returns <c>true</c> if the target node is an <see cref="InstancePlaceholder"/>.
		/// </summary>
		public bool IsInstancePlaceholder() => IsValid() && _state.IsNodeInstancePlaceholder(_idx);

		/// <summary>
		/// Returns the path to the represented scene file if the target node is an <see cref="InstancePlaceholder"/>.
		/// </summary>
		public string GetInstancePlaceholder() => IsValid() ? _state.GetNodeInstancePlaceholder(_idx) : "";

		/// <summary>
		/// Returns the path to the owner of the target node, relative to the root node of the scene file.
		/// </summary>
		public NodePath GetOwnerPath() => IsValid() ? _state.GetNodeOwnerPath(_idx) : new NodePath();

		/// <summary>
		/// Returns the node's index, which is its position relative to its siblings.
		/// <br/><br/>
		/// Possible return values:
		/// <br/>- <c>0</c>: The node is the first child of its parent (or the root of the scene).
		/// <br/>- <c>1</c>: The node is the second child, and so on.
		/// <br/>- <c>-1</c>: The <see cref="StateInspector"/> is invalid or the node path could not be resolved.
		/// </summary>
		public int GetNodeIndex() => IsValid() ? _state.GetNodeIndex(_idx) : -1;

		/// <summary>
		/// Returns the <see cref="SceneState"/> of the scene that this scene inherits from.
		/// </summary>
		public SceneState GetBaseSceneState() => _state?.GetBaseSceneState();

		/// <summary>
		/// Returns an array of dictionaries representing all signal connections originating from this node.
		/// </summary>
		public Godot.Collections.Array<Godot.Collections.Dictionary> GetConnections()
		{
			var connections = new Godot.Collections.Array<Godot.Collections.Dictionary>();
			if (IsValid())
			{
				string cleanTarget = _state.GetNodePath(_idx).ToString().TrimPrefix("./");
				if (_idx == 0 || string.IsNullOrEmpty(cleanTarget)) cleanTarget = ".";

				for (int c = 0; c < _state.GetConnectionCount(); c++)
				{
					string cleanSource = _state.GetConnectionSource(c).ToString().TrimPrefix("./");
					if (string.IsNullOrEmpty(cleanSource)) cleanSource = ".";

					if (cleanSource == cleanTarget)
					{
						var connDict = new Godot.Collections.Dictionary
						{
							{ "signal", _state.GetConnectionSignal(c) },
							{ "method", _state.GetConnectionMethod(c) },
							{ "target", _state.GetConnectionTarget(c) },
							{ "binds", _state.GetConnectionBinds(c) },
							{ "unbinds", _state.GetConnectionUnbinds(c) },
							{ "flags", (int)_state.GetConnectionFlags(c) }
						};
						connections.Add(connDict);
					}
				}
			}
			return connections;
		}
	}

	/// <summary>
	/// Internal helper to handle heavy SceneState lookups, timestamp caching, and deep sub-scene recursion.
	/// </summary>
	private class StateCache
	{
		private SceneState _state;
		private ulong _modifiedTime = 0;

		public SceneState GetValidState(string rawPath)
		{
			if (string.IsNullOrEmpty(rawPath)) return null;
			string realPath = SafeResolvePath(rawPath);
			if (string.IsNullOrEmpty(realPath) || !FileAccess.FileExists(realPath)) return null;

			ulong currentTime = FileAccess.GetModifiedTime(realPath);
			if (_state != null && _modifiedTime == currentTime) return _state;

			var packed = ResourceLoader.Load<PackedScene>(realPath);
			if (packed == null) return null;

			_state = packed.GetState();
			_modifiedTime = currentTime;
			return _state;
		}

		public (SceneState state, int idx) ResolveDeepNode(SceneState rootState, string targetPath)
		{
			if (rootState == null || string.IsNullOrEmpty(targetPath))
				return (null, -1);

			int directIdx = FindIdxInState(rootState, targetPath);
			if (directIdx != -1) return (rootState, directIdx);

			for (int i = 0; i < rootState.GetNodeCount(); i++)
			{
				PackedScene inst = rootState.GetNodeInstance(i);
				if (inst != null)
				{
					string instPath = rootState.GetNodePath(i).ToString().TrimPrefix("./");
					if (instPath == ".") continue;

					string prefix = instPath + "/";
					if (targetPath.StartsWith(prefix))
					{
						string remainder = targetPath.TrimPrefix(prefix);
						SceneState subState = inst.GetState();

						var result = ResolveDeepNode(subState, remainder);
						if (result.idx != -1)
							return result;
					}
				}
			}
			return (null, -1);
		}

		private int FindIdxInState(SceneState targetState, string currentPath)
		{
			NodePath exactNp = new NodePath(currentPath);
			NodePath relativeNp = new NodePath("./" + currentPath);

			if (currentPath == "." || currentPath == targetState.GetNodeName(0).ToString())
				return 0;

			for (int i = 0; i < targetState.GetNodeCount(); i++)
			{
				NodePath statePath = targetState.GetNodePath(i);
				if (statePath == exactNp || statePath == relativeNp)
					return i;
			}
			return -1;
		}
	}
}
