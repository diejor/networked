using System;
using System.Collections.Concurrent;
using System.Reflection;
using System.Runtime.CompilerServices;
using Godot;

namespace Networked;

internal static class NetwPropertyTypes
{
    private readonly record struct MemberType(long Type, string Error);

    private static readonly ConditionalWeakTable<
        Type, ConcurrentDictionary<string, MemberType>> Cache = new();

    internal static long Resolve(Node node, StringName name)
    {
        ArgumentNullException.ThrowIfNull(node);
        var members = Cache.GetValue(node.GetType(), _ => new());
        var found = members.GetOrAdd(name.ToString(),
            key => Find(node.GetType(), key));
        if (found.Error != null)
        {
            throw new ArgumentException(found.Error, nameof(name));
        }
        return found.Type;
    }

    private static MemberType Find(Type type, string name)
    {
        const BindingFlags flags = BindingFlags.Instance |
            BindingFlags.Public | BindingFlags.NonPublic |
            BindingFlags.DeclaredOnly;
        for (Type current = type; current != null;
             current = current.BaseType)
        {
            PropertyInfo property = current.GetProperty(name, flags);
            if (property != null && property.GetIndexParameters().Length == 0)
            {
                if (property.GetMethod == null || property.SetMethod == null)
                {
                    return new(-1,
                        $"Cannot configure property '{name}'. Provide " +
                        "both an instance getter and setter.");
                }
                return Mapped(property.PropertyType, name);
            }
            FieldInfo field = current.GetField(name, flags);
            if (field != null)
            {
                if (field.IsInitOnly || field.IsLiteral)
                {
                    return new(-1,
                        $"Cannot configure field '{name}'. Make it writable.");
                }
                return Mapped(field.FieldType, name);
            }
        }
        return new(-1, null);
    }

    private static MemberType Mapped(Type type, string name)
    {
        long mapped = VariantType(type);
        return mapped >= 0 ? new(mapped, null) : new(-1,
            $"Cannot configure property '{name}' of type '{type.Name}'. " +
            "Use a Godot Variant-compatible type.");
    }

    private static long VariantType(Type type)
    {
        if (type.IsEnum || type == typeof(char) || type == typeof(sbyte) ||
            type == typeof(byte) || type == typeof(short) ||
            type == typeof(ushort) || type == typeof(int) ||
            type == typeof(uint) || type == typeof(long) ||
            type == typeof(ulong)) return (long)Variant.Type.Int;
        if (type == typeof(bool)) return (long)Variant.Type.Bool;
        if (type == typeof(float) || type == typeof(double))
            return (long)Variant.Type.Float;
        if (type == typeof(string)) return (long)Variant.Type.String;
        if (type == typeof(Variant)) return (long)Variant.Type.Nil;
        if (typeof(GodotObject).IsAssignableFrom(type))
            return (long)Variant.Type.Object;
        if (type.Assembly == typeof(Vector2).Assembly &&
            type.Namespace == "Godot" &&
            Enum.TryParse(type.Name, out Variant.Type kind)) return (long)kind;
        if (type.Assembly == typeof(Godot.Collections.Array).Assembly &&
            type.Namespace == "Godot.Collections")
        {
            if (type == typeof(Godot.Collections.Array) ||
                type.IsGenericType && type.GetGenericTypeDefinition() ==
                    typeof(Godot.Collections.Array<>))
                return (long)Variant.Type.Array;
            if (type == typeof(Godot.Collections.Dictionary) ||
                type.IsGenericType && type.GetGenericTypeDefinition() ==
                    typeof(Godot.Collections.Dictionary<,>))
                return (long)Variant.Type.Dictionary;
        }
        if (!type.IsArray || type.GetArrayRank() != 1) return -1;
        Type element = type.GetElementType();
        if (element == typeof(byte)) return (long)Variant.Type.PackedByteArray;
        if (element == typeof(int)) return (long)Variant.Type.PackedInt32Array;
        if (element == typeof(long)) return (long)Variant.Type.PackedInt64Array;
        if (element == typeof(float))
            return (long)Variant.Type.PackedFloat32Array;
        if (element == typeof(double))
            return (long)Variant.Type.PackedFloat64Array;
        if (element == typeof(string)) return (long)Variant.Type.PackedStringArray;
        if (element == typeof(Vector2))
            return (long)Variant.Type.PackedVector2Array;
        if (element == typeof(Vector3))
            return (long)Variant.Type.PackedVector3Array;
        if (element == typeof(Vector4))
            return (long)Variant.Type.PackedVector4Array;
        if (element == typeof(Color)) return (long)Variant.Type.PackedColorArray;
        if (typeof(GodotObject).IsAssignableFrom(element) ||
            element == typeof(StringName) || element == typeof(NodePath) ||
            element == typeof(Rid)) return (long)Variant.Type.Array;
        return -1;
    }
}
