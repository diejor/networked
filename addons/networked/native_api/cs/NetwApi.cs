using System;
using System.Runtime.InteropServices;
using Godot;
using Godot.NativeInterop;

namespace Networked;

/// <summary>
/// The dispatch table the addon exports, loaded once on first use.
/// </summary>
/// <remarks>
/// Every generated binding calls through here. The table is fetched with a
/// single <c>ClassDB.ClassCallStatic</c> and then never again, so a call
/// costs a delegate invocation rather than a name lookup.
/// </remarks>
public static class NetwApi
{
    public const long SupportedVersion = 3;

    private static readonly string[] Exported =
    {
        "method_bind",
        "ptrcall0", "ptrcall1", "ptrcall2", "ptrcall3",
        "call0", "call1", "call2", "call3", "call4",
        "args_new", "args_set", "call_pack", "args_free",
        "retain", "release",
    };

    private delegate IntPtr MethodBindDelegate(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string className,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string methodName,
        ulong hash);

    private delegate void ReferenceDelegate(IntPtr instance);

    private static MethodBindDelegate _methodBind;
    private static Godot.Collections.Dictionary _functions;
    private static ReferenceDelegate _retain;
    private static ReferenceDelegate _release;
    private static bool _loaded;
    private static string _failure;

    /// <summary>Whether the addon answered and its table matched.</summary>
    public static bool IsAvailable
    {
        get
        {
            Load();
            return _failure == null;
        }
    }

    /// <summary>Why the table could not be used, or null.</summary>
    public static string Failure
    {
        get
        {
            Load();
            return _failure;
        }
    }

    private static void Load()
    {
        if (_loaded)
        {
            return;
        }
        _loaded = true;

        if (!ClassDB.ClassExists("Netw"))
        {
            _failure = "the Networked addon is not installed in this project";
            return;
        }

        Variant answered = ClassDB.ClassCallStatic("Netw", "_native_api");
        if (answered.VariantType != Variant.Type.Dictionary)
        {
            _failure = "Netw._native_api answered no table";
            return;
        }

        var table = answered.AsGodotDictionary();
        long version = table["version"].AsInt64();
        if (version != SupportedVersion)
        {
            _failure =
                $"the addon exports native API version {version}, and " +
                $"these bindings were generated for {SupportedVersion}";
            return;
        }

        long hash = table["hash"].AsInt64();
        if (hash != ExpectedHash())
        {
            _failure = "the addon exports a different set of native API " +
                       "functions than these bindings were generated for";
            return;
        }

        bool isDouble = table["is_double"].AsBool();
        if (isDouble != NetwBuild.IsDoublePrecision)
        {
            _failure =
                "the addon and these bindings disagree about float width";
            return;
        }

        _functions = table["functions"].AsGodotDictionary();
        _methodBind = Bind<MethodBindDelegate>(_functions, "method_bind");
        _retain = Bind<ReferenceDelegate>(_functions, "retain");
        _release = Bind<ReferenceDelegate>(_functions, "release");
    }

    private static long ExpectedHash()
    {
        ulong hash = 14695981039346656037UL;
        foreach (string name in Exported)
        {
            foreach (char at in name)
            {
                hash ^= (byte)at;
                hash *= 1099511628211UL;
            }
            hash ^= (ulong)SupportedVersion;
            hash *= 1099511628211UL;
        }
        return (long)(hash & 0x7fffffffffffffffUL);
    }

    private static T Bind<T>(
        Godot.Collections.Dictionary functions, string name)
        where T : Delegate
    {
        long address = functions[name].AsInt64();
        if (address == 0)
        {
            _failure = $"the native API table carries no address for '{name}'";
            return null;
        }
        return Marshal.GetDelegateForFunctionPointer<T>((IntPtr)address);
    }

    private static void Require()
    {
        Load();
        if (_failure != null)
        {
            throw new InvalidOperationException("Networked: " + _failure);
        }
    }

    public static IntPtr MethodBind(
        string className, string methodName, ulong hash)
    {
        Require();
        IntPtr bind = _methodBind(className, methodName, hash);
        if (bind == IntPtr.Zero)
        {
            throw new MissingMethodException(
                $"Networked: {className}.{methodName} is not bound with hash " +
                $"{hash}. The generated bindings are older or newer than the " +
                "installed addon.");
        }
        return bind;
    }

    /// <summary>
    /// One fixed-arity entry point from the table, bound on first use.
    /// </summary>
    internal static T Thunk<T>(string name) where T : Delegate
    {
        Require();
        T found = Bind<T>(_functions, name);
        if (found == null)
        {
            throw new InvalidOperationException("Networked: " + _failure);
        }
        return found;
    }

    /// <summary>
    /// Takes a reference on a reference-counted object and answers it back, so
    /// a handle adopted from a call outlives the value the call answered.
    /// </summary>
    public static IntPtr Retained(IntPtr instance)
    {
        if (instance != IntPtr.Zero)
        {
            Require();
            _retain(instance);
        }
        return instance;
    }

    public static void Release(IntPtr instance)
    {
        _release(instance);
    }

    /// <summary>
    /// The object a Variant holds, or zero when it holds none.
    /// </summary>
    public static IntPtr ObjectOf(Variant value)
    {
        godot_variant held = value.CopyNativeVariant();
        IntPtr found = VariantUtils.ConvertToGodotObjectPtr(held);
        held.Dispose();
        return found;
    }
}
