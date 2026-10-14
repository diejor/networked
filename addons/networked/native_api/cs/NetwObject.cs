using System;
using Godot;
using Godot.NativeInterop;

namespace Networked;

/// <summary>
/// Whether this build of the bindings was compiled for doubles.
/// </summary>
public static class NetwBuild
{
#if GODOT_REAL_T_IS_DOUBLE
    public const bool IsDoublePrecision = true;
#else
    public const bool IsDoublePrecision = false;
#endif
}

/// <summary>
/// A handle to an object owned by the Networked addon.
/// </summary>
/// <remarks>
/// The handle holds the engine pointer rather than a
/// <see cref="Godot.GodotObject"/>, because these classes live in the addon's
/// ClassDB entries and not in GodotSharp. Instances that came from a
/// reference-counted call are released when the handle is disposed.
/// </remarks>
public abstract class NetwObject : IDisposable
{
    private IntPtr _native;
    private readonly bool _owned;

    protected NetwObject(IntPtr native, bool owned)
    {
        _native = native;
        _owned = owned;
    }

    /// <summary>The engine object pointer, or zero once disposed.</summary>
    public IntPtr Native => _native;

    /// <summary>Whether the handle still points at a live object.</summary>
    public bool IsValid => _native != IntPtr.Zero;

    /// <summary>
    /// A <see cref="Variant"/> holding the object, for a parameter typed
    /// <see cref="Variant"/>. A null handle gives a null Variant.
    /// </summary>
    public static implicit operator Variant(NetwObject value)
    {
        return Variant.CreateTakingOwnershipOfDisposableValue(
            VariantUtils.CreateFromGodotObjectPtr(
                value?.Native ?? IntPtr.Zero));
    }

    protected IntPtr Checked
    {
        get
        {
            if (_native == IntPtr.Zero)
            {
                throw new ObjectDisposedException(GetType().Name);
            }
            return _native;
        }
    }

    private static readonly IntPtr BindConnect =
        NetwApi.MethodBind("Object", "connect", 1518946055UL);

    private static readonly IntPtr BindDisconnect =
        NetwApi.MethodBind("Object", "disconnect", 1874754934UL);

    private static readonly IntPtr BindIsConnected =
        NetwApi.MethodBind("Object", "is_connected", 768136979UL);

    /// <summary>
    /// Subscribes a handler to one of this object's signals. The generated
    /// events call through here, so a game writes
    /// <c>entity.Spawned += OnSpawned</c> rather than naming the signal.
    /// </summary>
    public Error Connect(StringName signal, Callable handler, long flags = 0)
    {
        godot_variant slot0 = VariantUtils.CreateFromStringName(signal);
        godot_variant slot1 = VariantUtils.CreateFromCallable(handler);
        godot_variant slot2 = VariantUtils.CreateFromInt(flags);
        godot_variant answered = default;
        NetwThunks.Call3(
            BindConnect, Checked, in slot0, in slot1, in slot2, ref answered);
        slot0.Dispose();
        slot1.Dispose();
        slot2.Dispose();
        var result = (Error)VariantUtils.ConvertToInt64(answered);
        answered.Dispose();
        return result;
    }

    public void Disconnect(StringName signal, Callable handler)
    {
        Reach(BindDisconnect, signal, handler);
    }

    private static readonly IntPtr BindInstanceId =
        NetwApi.MethodBind("Object", "get_instance_id", 3905245786UL);

    private static readonly System.Collections.Generic.Dictionary<
        (ulong Instance, string Signal),
        System.Collections.Generic.List<(Delegate Handler, Callable Bridge)>
    > Bridges = new();

    /// <summary>The engine's instance id for this object.</summary>
    public ulong InstanceId
    {
        get
        {
            godot_variant answered = default;
            NetwThunks.Call0(BindInstanceId, Checked, ref answered);
            ulong id = (ulong)VariantUtils.ConvertToInt64(answered);
            answered.Dispose();
            return id;
        }
    }

    /// <summary>
    /// Connects a typed event handler through a <paramref name="bridge"/>
    /// that converts the signal's Variant arguments. The bridge is kept per
    /// engine object, so removing the handler through any wrapper of the same
    /// object disconnects it.
    /// </summary>
    protected void ConnectBridge(string signal, Delegate handler, Callable bridge)
    {
        var key = (InstanceId, signal);
        lock (Bridges)
        {
            Prune();
            if (!Bridges.TryGetValue(key, out var held))
            {
                held = new System.Collections.Generic.List<(Delegate, Callable)>();
                Bridges[key] = held;
            }
            held.Add((handler, bridge));
        }
        Connect(signal, bridge);
    }

    /// <summary>
    /// Disconnects the bridge the most recent matching
    /// <see cref="ConnectBridge"/> made for <paramref name="handler"/>.
    /// </summary>
    protected void DisconnectBridge(string signal, Delegate handler)
    {
        var key = (InstanceId, signal);
        Callable bridge;
        lock (Bridges)
        {
            if (!Bridges.TryGetValue(key, out var held))
            {
                return;
            }
            int at = held.FindLastIndex(entry => entry.Handler == handler);
            if (at < 0)
            {
                return;
            }
            bridge = held[at].Bridge;
            held.RemoveAt(at);
            if (held.Count == 0)
            {
                Bridges.Remove(key);
            }
        }
        if (IsConnected(signal, bridge))
        {
            Disconnect(signal, bridge);
        }
    }

    private static void Prune()
    {
        var gone = new System.Collections.Generic.List<(ulong, string)>();
        foreach (var key in Bridges.Keys)
        {
            if (!GodotObject.IsInstanceIdValid(key.Instance))
            {
                gone.Add(key);
            }
        }
        foreach (var key in gone)
        {
            Bridges.Remove(key);
        }
    }

    /// <summary>
    /// The wrapper of the addon object a signal argument carries, or
    /// <see langword="null"/> when it carries none.
    /// </summary>
    protected static T Wrapped<T>(Variant value, Func<Variant, T> wrap)
        where T : class
    {
        return value.VariantType == Variant.Type.Nil ? null : wrap(value);
    }

    public bool IsConnected(StringName signal, Callable handler)
    {
        return Reach(BindIsConnected, signal, handler);
    }

    private bool Reach(IntPtr bind, StringName signal, Callable handler)
    {
        godot_variant slot0 = VariantUtils.CreateFromStringName(signal);
        godot_variant slot1 = VariantUtils.CreateFromCallable(handler);
        godot_variant answered = default;
        NetwThunks.Call2(bind, Checked, in slot0, in slot1, ref answered);
        slot0.Dispose();
        slot1.Dispose();
        bool result = VariantUtils.ConvertToBool(answered);
        answered.Dispose();
        return result;
    }

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    protected virtual void Dispose(bool disposing)
    {
        if (_native == IntPtr.Zero)
        {
            return;
        }
        if (_owned)
        {
            NetwApi.Release(_native);
        }
        _native = IntPtr.Zero;
    }

    ~NetwObject()
    {
        Dispose(false);
    }
}

/// <summary>
/// A handle to a reference-counted object owned by the addon.
/// </summary>
public abstract class NetwRefCounted : NetwObject
{
    protected NetwRefCounted(IntPtr native) : base(native, owned: true)
    {
    }
}

public abstract class NetwNode : NetwObject
{
    protected NetwNode(IntPtr native) : base(native, owned: false)
    {
    }

    public Node Node => GodotObject.InstanceFromId(InstanceId) as Node;

    public void Free()
    {
        Node.Free();
        Dispose();
    }
}
