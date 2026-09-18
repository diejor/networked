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
