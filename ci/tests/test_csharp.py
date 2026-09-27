"""Fixtures for the encodings a generated binding gets wrong silently."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import csharp  # noqa: E402
import csharp_docs  # noqa: E402
from common import CIError  # noqa: E402


def dump_of(*classes: dict) -> dict:
    return {"classes": list(classes)}


def clock_class() -> dict:
    return {
        "name": "NetwClockHandle",
        "api_type": "extension",
        "is_refcounted": True,
        "inherits": "RefCounted",
        "methods": [
            {
                "name": "get_tick",
                "hash": 111,
                "return_value": {"type": "int", "meta": "int64"},
            },
            {
                "name": "get_reference_count",
                "hash": 222,
                "return_value": {"type": "int", "meta": "int32"},
            },
            {
                "name": "param",
                "hash": 333,
                "return_value": {"type": "Variant"},
                "arguments": [{"name": "param", "type": "int", "meta": "int64"}],
            },
            {
                "name": "monitor",
                "hash": 444,
                "return_value": {"type": "float"},
                "arguments": [{"name": "event", "type": "int", "meta": "int64"}],
            },
        ],
        "properties": [{"type": "int", "name": "tick", "getter": "get_tick"}],
    }


def promise_class() -> dict:
    return {
        "name": "NetwPromise",
        "api_type": "extension",
        "is_refcounted": True,
        "inherits": "RefCounted",
        "methods": [],
        "properties": [],
    }


def tree_class() -> dict:
    return {
        "name": "MultiplayerTree",
        "api_type": "extension",
        "is_refcounted": False,
        "inherits": "Node",
        "methods": [{"name": "dispose", "hash": 900, "return_value": None}],
        "properties": [],
    }


def facade_class() -> dict:
    return {
        "name": "Netw",
        "api_type": "extension",
        "is_refcounted": True,
        "inherits": "RefCounted",
        "methods": [
            {
                "name": "spawn",
                "hash": 11,
                "is_static": True,
                "is_vararg": True,
                "return_value": {"type": "NetwPromise"},
                "arguments": [{"name": "node", "type": "Node"}],
            },
            {
                "name": "scene",
                "hash": 12,
                "is_static": True,
                "return_value": {"type": "MultiplayerTree"},
                "arguments": [{"name": "name", "type": "StringName"}],
            },
            {
                "name": "get_lane",
                "hash": 13,
                "return_value": {"type": "int", "meta": "int32"},
                "arguments": [{"name": "index", "type": "int", "meta": "int32"}],
            },
            {
                "name": "set_lane",
                "hash": 14,
                "return_value": {"type": "enum::Error"},
                "arguments": [
                    {"name": "index", "type": "int", "meta": "int32"},
                    {"name": "value", "type": "int", "meta": "int32"},
                ],
            },
        ],
        "properties": [
            {
                "type": "int",
                "name": "first_lane",
                "getter": "get_lane",
                "setter": "set_lane",
                "index": 0,
            },
            {
                "type": "int",
                "name": "second_lane",
                "getter": "get_lane",
                "setter": "set_lane",
                "index": 1,
            },
        ],
    }


def entity_class() -> dict:
    return {
        "name": "NetwEntity",
        "api_type": "extension",
        "is_refcounted": True,
        "inherits": "RefCounted",
        "methods": [
            {
                "name": "set_ownership",
                "hash": 21,
                "return_value": None,
                "arguments": [{"name": "value", "type": "enum::NetwEntity.Ownership"}],
            },
            {
                "name": "get_ownership",
                "hash": 22,
                "return_value": {"type": "enum::NetwEntity.Ownership"},
            },
            {
                "name": "capabilities",
                "hash": 23,
                "return_value": {"type": "bitfield::NetwEntity.Capability"},
            },
            {
                "name": "last_error",
                "hash": 24,
                "return_value": {"type": "enum::Error"},
            },
        ],
        "properties": [
            {
                "type": "int",
                "name": "ownership",
                "getter": "get_ownership",
                "setter": "set_ownership",
            }
        ],
        "signals": [
            {"name": "spawned", "arguments": []},
            {
                "name": "interest_enter",
                "arguments": [
                    {"name": "peer", "type": "int", "meta": "int64"},
                    {"name": "holder", "type": "NetwEntity"},
                ],
            },
        ],
        "enums": [
            {
                "name": "Ownership",
                "is_bitfield": False,
                "values": [
                    {"name": "OWNERSHIP_NONE", "value": 0},
                    {"name": "OWNERSHIP_DEDICATED_SERVER", "value": 1},
                ],
            },
            {
                "name": "Capability",
                "is_bitfield": True,
                "values": [
                    {"name": "CAPABILITY_BROWSE", "value": 1},
                    {"name": "CAPABILITY_INVITES", "value": 2},
                ],
            },
        ],
        "constants": [{"name": "MAX_LANES", "value": 8}],
    }


def whole_dump() -> dict:
    return dump_of(
        clock_class(),
        promise_class(),
        tree_class(),
        facade_class(),
        entity_class(),
        {"name": "Node", "api_type": "core"},
        {"name": "Object", "api_type": "core"},
        {"name": "MultiplayerAPI", "api_type": "core"},
        {"name": "RefCounted", "api_type": "core"},
    )


class Naming(unittest.TestCase):
    def test_pascal_keeps_a_leading_underscore(self):
        self.assertEqual(csharp.pascal("_ready"), "_Ready")
        self.assertEqual(csharp.pascal("get_tick"), "GetTick")

    def test_camel_escapes_a_keyword(self):
        self.assertEqual(csharp.camel("event"), "@event")
        self.assertEqual(csharp.camel("max_future_action"), "maxFutureAction")


class Encodings(unittest.TestCase):
    def setUp(self):
        self.surface = csharp.Surface(whole_dump())

    def test_an_int_takes_the_slot_its_meta_names(self):
        wide = self.surface.encode({"type": "int", "meta": "int64"})
        narrow = self.surface.encode({"type": "int", "meta": "int32"})
        self.assertEqual(wide.slot, "long")
        self.assertEqual(narrow.slot, "int")

    def test_an_int_with_no_meta_is_eight_bytes(self):
        self.assertEqual(self.surface.encode({"type": "int"}).slot, "long")

    def test_a_bool_crosses_as_a_byte(self):
        encoded = self.surface.encode({"type": "bool"})
        self.assertEqual(encoded.declared, "bool")
        self.assertEqual(encoded.slot, "byte")

    def test_an_enum_crosses_as_a_wide_int(self):
        self.assertEqual(self.surface.encode({"type": "enum::Error"}).slot, "long")

    def test_an_object_argument_crosses_as_a_pointer(self):
        self.assertEqual(self.surface.encode({"type": "Node"}).slot, "IntPtr")

    def test_an_object_return_refuses_the_ptrcall(self):
        self.assertIsNone(self.surface.encode({"type": "Node"}, "return"))

    def test_a_type_with_no_ptrcall_encoding_is_refused(self):
        for unsupported in ("String", "StringName", "Dictionary", "Variant"):
            self.assertIsNone(self.surface.encode({"type": unsupported}))


class Boxes(unittest.TestCase):
    def setUp(self):
        self.surface = csharp.Surface(whole_dump())

    def test_every_type_the_ptrcall_refuses_still_boxes(self):
        for refused in ("String", "StringName", "Dictionary", "Variant"):
            self.assertIsNotNone(self.surface.box({"type": refused}))

    def test_a_typed_array_boxes_as_an_untyped_one(self):
        boxed = self.surface.box({"type": "typedarray::NetwEntity"})
        self.assertEqual(boxed.declared, "Godot.Collections.Array")

    def test_an_int_box_casts_before_it_widens(self):
        boxed = self.surface.box({"type": "int", "meta": "int32"})
        self.assertEqual(boxed.declared, "int")
        self.assertIn("(long)", boxed.to_variant)
        self.assertIn("ConvertToInt32", boxed.from_variant)

    def test_a_refcounted_answer_is_retained_before_it_is_adopted(self):
        boxed = self.surface.box({"type": "NetwPromise"})
        self.assertEqual(
            boxed.from_variant,
            "NetwPromise.Adopt(NetwApi.Retained(" "VariantUtils.ConvertToGodotObjectPtr({name})))",
        )

    def test_an_unowned_answer_is_adopted_without_a_reference(self):
        boxed = self.surface.box({"type": "MultiplayerTree"})
        self.assertNotIn("Retained", boxed.from_variant)

    def test_an_own_class_answer_crosses_a_ptrcall(self):
        encoded = self.surface.encode({"type": "NetwPromise"}, "return")
        self.assertEqual(encoded.slot, "IntPtr")
        self.assertEqual(encoded.from_slot, "NetwPromise.Adopt({name})")

    def test_a_ptrcall_answer_is_adopted_without_retaining_it(self):
        """The engine's own Ref assignment into the return slot takes the
        reference, so retaining again would leak one."""
        encoded = self.surface.encode({"type": "NetwPromise"}, "return")
        self.assertNotIn("Retained", encoded.from_slot)

    def test_an_engine_class_answer_stays_on_the_call_path(self):
        """A GodotSharp wrapper cannot be built from a raw pointer without an
        internal, so only this addon's own classes move."""
        self.assertIsNone(self.surface.encode({"type": "Node"}, "return"))

    def test_an_engine_class_takes_its_csharp_spelling(self):
        self.assertEqual(self.surface.box({"type": "Object"}).declared, "GodotObject")
        self.assertEqual(self.surface.box({"type": "MultiplayerAPI"}).declared, "MultiplayerApi")

    def test_a_type_the_tables_do_not_carry_is_refused(self):
        self.assertIsNone(self.surface.box({"type": "NoSuchType"}))


class Folding(unittest.TestCase):
    def test_a_short_statement_is_left_alone(self):
        self.assertEqual(csharp.fold("    ", "int x = 1;"), ["    int x = 1;"])

    def test_an_assignment_breaks_after_its_equals(self):
        call = "Some.Very.Long.Call(with, several, arguments, here, and, more)"
        lines = csharp.fold(" " * 12, "SomeLongTypeName result = %s;" % call)
        self.assertTrue(lines[0].endswith("="))
        self.assertTrue(all(len(line) <= csharp.WIDTH for line in lines))

    def test_a_signature_breaks_one_argument_to_a_line(self):
        signature = (
            "public static NetwPromise SpawnPlayerUnderTheLongName("
            "Node parent, StringName scene, Godot.Collections.Dictionary opts)"
        )
        lines = csharp.fold("    ", signature)
        self.assertEqual(len(lines), 4)
        self.assertTrue(lines[-1].endswith(")"))
        self.assertTrue(all(len(line) <= csharp.WIDTH for line in lines))

    def test_a_nested_call_is_one_piece(self):
        self.assertEqual(csharp.split_arguments("a, f(b, c), d"), ["a", "f(b, c)", "d"])


class Emission(unittest.TestCase):
    def setUp(self):
        self.surface = csharp.Surface(whole_dump())
        self.emitter = csharp.Emitter(self.surface)
        self.clock = self.emitter.emit(clock_class())
        self.facade = self.emitter.emit(facade_class())
        self.tree = self.emitter.emit(tree_class())

    def test_a_property_reads_through_its_getter_bind(self):
        self.assertIn("public long Tick", self.clock)
        self.assertIn('MethodBind("NetwClockHandle", "get_tick", 111UL)', self.clock)

    def test_an_accessor_is_not_also_emitted_as_a_method(self):
        self.assertNotIn("public long GetTick(", self.clock)

    def test_a_variant_method_crosses_on_the_call_path(self):
        self.assertIn("public Variant Param(long param)", self.clock)
        self.assertIn("NetwThunks.Call1(_bindParam", self.clock)
        self.assertNotIn("NetwClockHandle.param", self.emitter.skipped)

    def test_an_argument_is_handed_over_by_reference(self):
        self.assertIn("godot_variant slot0 = ", self.clock)
        self.assertIn("in slot0", self.clock)

    def test_no_emitted_body_carries_a_pointer(self):
        for emitted in (self.clock, self.facade):
            for spelling in ("unsafe", "stackalloc", "void**", "= &"):
                self.assertNotIn(spelling, emitted)

    def test_a_keyword_argument_is_escaped(self):
        self.assertIn("double Monitor(long @event)", self.clock)

    def test_a_vararg_method_takes_a_params_array(self):
        self.assertIn("params Variant[] rest", self.facade)
        self.assertIn("rest[index - 1].CopyNativeVariant()", self.facade)

    def test_a_static_method_carries_no_instance(self):
        self.assertIn("public static NetwPromise Spawn(", self.facade)
        self.assertIn("CallPack(_bindSpawn, IntPtr.Zero, pack", self.facade)

    def test_every_variant_slot_is_disposed_after_the_call(self):
        self.assertIn("carried.Dispose();", self.facade)
        self.assertIn("slot0.Dispose();", self.facade)
        self.assertIn("answered.Dispose();", self.facade)

    def test_a_vararg_tail_frees_the_pack_it_allocated(self):
        self.assertIn("IntPtr pack = NetwThunks.ArgsNew(total);", self.facade)
        self.assertIn("NetwThunks.ArgsFree(pack);", self.facade)

    def test_a_setter_discards_what_its_bind_answers(self):
        written = self.facade.split("public int FirstLane")[1].split("set")[1]
        self.assertNotIn("return", written.split("    }")[0])

    def test_an_indexed_property_passes_its_index(self):
        self.assertIn("int slot0 = 0;", self.facade)
        self.assertIn("int slot0 = 1;", self.facade)

    def test_one_bind_field_serves_every_member_that_calls_it(self):
        held = "private static readonly IntPtr _bindGetLane"
        self.assertEqual(self.facade.count(held), 1)

    def test_a_bind_field_cannot_collide_with_a_member_name(self):
        for line in self.facade.splitlines():
            if "private static readonly IntPtr" in line:
                self.assertIn("IntPtr _bind", line)

    def test_a_member_colliding_with_the_handle_is_refused(self):
        self.assertNotIn("public void Dispose()", self.tree)
        self.assertIn("MultiplayerTree.dispose", self.emitter.skipped)

    def test_an_unowned_class_holds_no_reference(self):
        self.assertIn("base(native, owned: false)", self.tree)

    def test_every_emitted_line_fits_the_column_limit(self):
        for text in (self.clock, self.facade, self.tree):
            for line in text.splitlines():
                self.assertLessEqual(len(line), csharp.WIDTH, line)


class Vocabulary(unittest.TestCase):
    def setUp(self):
        self.surface = csharp.Surface(whole_dump())
        self.emitter = csharp.Emitter(self.surface)
        self.entity = self.emitter.emit(entity_class())

    def test_an_enum_constant_drops_the_prefix_its_own_name_repeats(self):
        self.assertIn("DedicatedServer = 1,", self.entity)
        self.assertNotIn("OwnershipDedicatedServer", self.entity)

    def test_a_bitfield_carries_the_flags_attribute(self):
        self.assertIn("    [Flags]\n    public enum Capability : long", self.entity)

    def test_an_enum_colliding_with_a_property_takes_a_suffix(self):
        self.assertIn("public enum OwnershipEnum : long", self.entity)
        self.assertIn("public NetwEntity.OwnershipEnum Ownership", self.entity)

    def test_an_enum_argument_declares_the_enum_rather_than_an_integer(self):
        self.assertEqual(
            self.surface.encode({"type": "enum::NetwEntity.Ownership"}).declared,
            "NetwEntity.OwnershipEnum",
        )

    def test_an_enum_still_crosses_in_a_wide_integer_slot(self):
        encoded = self.surface.encode({"type": "enum::NetwEntity.Ownership"})
        self.assertEqual(encoded.slot, "long")
        self.assertEqual(encoded.to_slot, "(long){name}")

    def test_a_global_enum_keeps_its_engine_spelling(self):
        self.assertEqual(self.surface.encode({"type": "enum::Error"}).declared, "Error")

    def test_an_enum_no_class_declares_crosses_as_an_integer(self):
        encoded = self.surface.encode({"type": "enum::NoSuchClass.Mode"})
        self.assertEqual(encoded.declared, "long")

    def test_a_constant_is_published_beside_the_enums(self):
        self.assertIn("public const long MaxLanes = 8;", self.entity)

    def test_a_signal_becomes_an_event_naming_its_godot_signal(self):
        self.assertIn("public event Action Spawned", self.entity)
        self.assertIn('add => Connect("spawned", Callable.From(value));', self.entity)

    def test_a_signal_carrying_a_handle_arrives_as_a_variant(self):
        self.assertIn("public event Action<long, Variant> InterestEnter", self.entity)

    def test_a_refcounted_handle_is_retained_when_it_comes_from_a_variant(self):
        self.assertIn("return Adopt(NetwApi.Retained(NetwApi.ObjectOf(value)));", self.entity)

    def test_an_unowned_handle_is_not_retained(self):
        tree = self.emitter.emit(tree_class())
        self.assertIn("return Adopt(NetwApi.ObjectOf(value));", tree)


class Ownership(unittest.TestCase):
    """Every GDExtension a project installs lands in the dump as api_type
    'extension', so the registry is what tells this addon's classes apart."""

    REGISTRY = """
void initialize_networked_module() {
    GDREGISTER_CLASS(netw::Netw);
    GDREGISTER_ABSTRACT_CLASS(netw::NetwAuthProtocol);
    GDREGISTER_INTERNAL_CLASS(netw::connect::WebRTCLink);
#if defined(NETW_TESTS) && defined(NETW_GDEXTENSION)
    GDREGISTER_CLASS(netw_test::Carrier);
#endif
#if defined(NETW_TESTS)
#if defined(NETW_GDEXTENSION)
    GDREGISTER_CLASS(netw_test::SpawnIdentityProbe);
#endif
    GDREGISTER_CLASS(netw::NetwNativeTests);
#else
    GDREGISTER_CLASS(netw::MultiplayerTree);
#endif
}
"""

    def registry_file(self, workspace: str) -> Path:
        path = Path(workspace) / "register_types.cpp"
        path.write_text(self.REGISTRY, encoding="utf-8")
        return path

    def test_the_registry_answers_what_the_addon_publishes(self):
        with tempfile.TemporaryDirectory() as workspace:
            names = csharp.registered_classes(self.registry_file(workspace))
        self.assertEqual(names, {"Netw", "NetwAuthProtocol", "MultiplayerTree"})

    def test_a_class_registered_only_under_the_test_flag_is_not_published(self):
        with tempfile.TemporaryDirectory() as workspace:
            names = csharp.registered_classes(self.registry_file(workspace))
        for stand in ("Carrier", "SpawnIdentityProbe", "NetwNativeTests"):
            self.assertNotIn(stand, names)

    def test_an_internal_registration_is_not_published(self):
        with tempfile.TemporaryDirectory() as workspace:
            names = csharp.registered_classes(self.registry_file(workspace))
        self.assertNotIn("WebRTCLink", names)

    def test_this_tree_publishes_none_of_its_own_test_stands(self):
        names = csharp.registered_classes()
        for stand in (
            "Carrier",
            "NetwNativeTests",
            "NetwTestAuthFlow",
            "NetwTestPersistenceEngine",
            "RecordingStepper",
            "SpawnIdentityProbe",
        ):
            self.assertNotIn(stand, names)

    def test_a_registry_with_no_registrations_is_refused(self):
        with tempfile.TemporaryDirectory() as workspace:
            path = Path(workspace) / "register_types.cpp"
            path.write_text("int main() { return 0; }", encoding="utf-8")
            with self.assertRaises(CIError):
                csharp.registered_classes(path)

    def test_another_extensions_classes_are_not_this_addons(self):
        foreign = {
            "name": "SteamMultiplayerPeer",
            "api_type": "extension",
            "is_refcounted": True,
            "methods": [],
            "properties": [],
        }
        dump = dump_of(facade_class(), foreign)
        surface = csharp.Surface(dump, {"Netw"})
        self.assertEqual(set(surface.classes), {"Netw"})

    def test_a_dump_carrying_none_of_the_registry_is_refused(self):
        foreign = {
            "name": "SteamMultiplayerPeer",
            "api_type": "extension",
            "is_refcounted": True,
            "methods": [],
            "properties": [],
        }
        with self.assertRaises(CIError):
            csharp.Surface(dump_of(foreign), {"Netw"})


class Refusals(unittest.TestCase):
    def test_a_dump_with_no_extension_classes_is_refused(self):
        engine_only = dump_of({"name": "Node", "api_type": "core"})
        with self.assertRaises(CIError):
            csharp.Surface(engine_only)

    def test_an_unknown_class_name_is_refused(self):
        with tempfile.TemporaryDirectory() as workspace:
            dump = Path(workspace) / "api.json"
            dump.write_text(
                '{"classes": [%s]}' % '{"name": "NetwClockHandle", "api_type": "extension", '
                '"is_refcounted": true, "methods": [], "properties": []}',
                encoding="utf-8",
            )
            with self.assertRaises(CIError):
                csharp.emit_tree(dump, Path(workspace) / "out", ["NoSuchClass"])


class Defaults(unittest.TestCase):
    """An optional parameter in Godot must become one in C#, or a C# caller
    passes what a GDScript caller omits."""

    def test_a_reference_default_is_null(self):
        self.assertEqual(csharp.default_value("NetwQuantize", "null"), ("null", None))

    def test_a_variant_cannot_be_null_because_it_is_a_struct(self):
        self.assertEqual(csharp.default_value("Variant", "null"), ("default", None))

    def test_a_callable_takes_the_struct_default(self):
        self.assertEqual(csharp.default_value("Callable", "Callable()"), ("default", None))

    def test_a_number_is_a_literal(self):
        self.assertEqual(csharp.default_value("int", "1"), ("1", None))
        self.assertEqual(csharp.default_value("long", "-1"), ("-1", None))
        self.assertEqual(csharp.default_value("double", "5.0"), ("5.0", None))

    def test_a_float_takes_its_suffix(self):
        self.assertEqual(csharp.default_value("float", "0.0"), ("0.0f", None))

    def test_an_enum_default_is_cast_because_the_dump_says_0(self):
        self.assertEqual(csharp.default_value("Netw.SceneChange", "0"), ("(Netw.SceneChange)0", None))

    def test_a_string_default_is_a_literal(self):
        self.assertEqual(csharp.default_value("string", '""'), ('""', None))

    def test_a_value_c_sharp_cannot_spell_is_filled_on_entry(self):
        """A C# default must be a compile time constant and an empty
        Dictionary is not, so the parameter is null and the body fills it."""
        for declared, raw, filled in (
            ("StringName", '&""', 'new StringName("")'),
            ("Godot.Collections.Dictionary", "{}", "new Godot.Collections.Dictionary()"),
            ("Godot.Collections.Array", "[]", "new Godot.Collections.Array()"),
            ("byte[]", "PackedByteArray()", "System.Array.Empty<byte>()"),
            ("string[]", "PackedStringArray()", "System.Array.Empty<string>()"),
        ):
            self.assertEqual(csharp.default_value(declared, raw), ("null", filled))

    def test_an_unknown_default_leaves_the_parameter_required(self):
        """Refusing is the answer, because inventing a default would change
        what the method does."""
        self.assertEqual(csharp.default_value("Vector3", "Vector3(1, 2, 3)"), (None, None))


class Documentation(unittest.TestCase):
    """The prose crosses from doc_classes XML, so what is asserted here is the
    rewriting rather than the wording."""

    NAMES = {
        "method Node.get_parent": "Node.GetParent",
        "member Node.name": "Node.Name",
        "member Node.multiplayer_authority": "Node.GetMultiplayerAuthority",
        "method Callable.get_method": "Callable.Method",
        "constant MultiplayerPeer.CONNECTION_CONNECTING": "MultiplayerPeer.ConnectionStatus.Connecting",
        "signal MultiplayerPeer.peer_connected": "MultiplayerPeer.PeerConnected",
    }

    def setUp(self):
        self.prose = csharp_docs.Prose(
            exports={
                "NetwClockHandle": {"tick": "Tick", "get_tick": "Tick", "SYNC_MODE_SNAP": "SyncMode.Snap"},
                "Netw": {"clock": "Clock"},
            },
            classes={"NetwClockHandle", "Netw"},
            engine={"Node": "Node", "Object": "GodotObject", "MultiplayerPeer": "MultiplayerPeer"},
            builtins={
                "Dictionary": "Godot.Collections.Dictionary",
                "PackedByteArray": "byte[]",
                "Callable": "Callable",
            },
            names=self.NAMES,
        )

    def render(self, text, owner="NetwClockHandle", params=None):
        return self.prose.render(owner, text, params)

    def test_a_type_becomes_a_cref(self):
        self.assertEqual(self.render("[Netw]"), '<see cref="Netw"/>')

    def test_a_member_resolves_through_the_emitters_own_naming(self):
        self.assertEqual(self.render("[member tick]"), '<see cref="NetwClockHandle.Tick"/>')

    def test_an_accessor_resolves_to_the_property_it_became(self):
        """C2 renamed these, so a guess at the C# spelling would drift."""
        self.assertEqual(self.render("[method get_tick]"), '<see cref="NetwClockHandle.Tick"/>')

    def test_an_enum_constant_carries_its_enum(self):
        self.assertEqual(self.render("[constant SYNC_MODE_SNAP]"), '<see cref="NetwClockHandle.SyncMode.Snap"/>')

    def test_an_engine_method_follows_godots_spelling(self):
        self.assertEqual(self.render("[method Node.get_parent]"), '<see cref="Node.GetParent"/>')

    def test_an_engine_property_resolves_through_the_index(self):
        """It cannot be derived, because GodotSharp publishes some of Godot's
        properties as a method pair instead."""
        self.assertEqual(self.render("[member Node.name]"), '<see cref="Node.Name"/>')

    def test_an_engine_getter_resolves_to_the_property_it_became(self):
        self.assertEqual(self.render("[method Callable.get_method]"), '<see cref="Callable.Method"/>')

    def test_an_engine_property_that_is_a_method_pair_takes_the_method(self):
        self.assertEqual(
            self.render("[member Node.multiplayer_authority]"),
            '<see cref="Node.GetMultiplayerAuthority"/>',
        )

    def test_an_engine_constant_nests_under_its_enum(self):
        """CONNECTION_CONNECTING is ConnectionStatus.Connecting, and the
        shared prefix is not knowable from the one name."""
        self.assertEqual(
            self.render("[constant MultiplayerPeer.CONNECTION_CONNECTING]"),
            '<see cref="MultiplayerPeer.ConnectionStatus.Connecting"/>',
        )

    def test_an_engine_signal_resolves_as_an_event(self):
        self.assertEqual(
            self.render("[signal MultiplayerPeer.peer_connected]"),
            '<see cref="MultiplayerPeer.PeerConnected"/>',
        )

    def test_an_engine_name_absent_from_the_index_falls_back(self):
        self.assertEqual(self.render("[method Node.no_such_verb]"), "<c>Node.no_such_verb</c>")

    def test_an_underscore_leaf_never_resolves(self):
        """Node.Ready EXISTS as a signal, so a lookup for _ready could answer
        confidently with the wrong member and the compiler would accept it.
        The refusal happens before any lookup, in Prose and in the table
        writer both."""
        self.assertEqual(self.render("[method Node._ready]"), "<c>Node._ready</c>")
        self.assertEqual(self.render("[method _enter_tree]"), "<c>_enter_tree</c>")

    def test_an_array_type_has_no_cref_syntax(self):
        self.assertEqual(self.render("[PackedByteArray]"), "<c>PackedByteArray</c>")

    def test_an_unknown_name_falls_back_and_is_counted(self):
        self.assertEqual(self.render("[NetwServerBrowser]"), "<c>NetwServerBrowser</c>")
        self.assertEqual(len(self.prose.fallbacks), 1)

    def test_a_param_becomes_a_paramref_without_its_escape(self):
        rendered = self.render("[param event]", params={"event": "@event"})
        self.assertEqual(rendered, '<paramref name="event"/>')

    def test_a_bare_block_is_carried_whatever_its_language(self):
        """Godot's own bindings_generator keeps a bare [codeblock] verbatim
        and drops a GDScript twin only when a [csharp] one was authored."""
        rendered = self.render("before [codeblock]Netw.join()[/codeblock] after")
        self.assertIn("<code>", rendered)
        self.assertIn("Netw.join()", rendered)

    def test_a_language_neutral_block_is_carried(self):
        rendered = self.render("[codeblock lang=text]a -> b[/codeblock]")
        self.assertIn("<code>", rendered)
        self.assertIn("a -&gt; b", rendered)

    def test_a_dual_language_block_prefers_the_csharp_twin(self):
        rendered = self.render(
            "[codeblocks][gdscript]Netw.join()[/gdscript]" "[csharp]Netw.Join();[/csharp][/codeblocks]"
        )
        self.assertIn("Netw.Join();", rendered)
        self.assertNotIn("Netw.join()", rendered)
        self.assertEqual(self.prose.gdscript_twins_dropped, 1)

    def test_a_dual_language_block_with_no_csharp_twin_carries_nothing(self):
        rendered = self.render("[codeblocks][gdscript]Netw.join()[/gdscript][/codeblocks]")
        self.assertNotIn("Netw.join()", rendered)

    def test_emphasis_survives_because_an_authority_marker_needs_it(self):
        """AGENTS.md 3.9 writes a marker as [b]Server Only.[/b], and Godot
        maps emphasis to <b> rather than stripping it."""
        self.assertEqual(self.render("[b]Server Only.[/b]"), "<b>Server Only.</b>")

    def test_prose_is_escaped_but_a_payload_is_not(self):
        rendered = self.render("a < b & [code]x[/code]")
        self.assertIn("&lt;", rendered)
        self.assertIn("&amp;", rendered)
        self.assertIn("<c>x</c>", rendered)

    def test_a_code_block_is_not_torn_on_its_own_blank_line(self):
        """A block's body carries blank lines, and splitting one produced
        <para> interleaved with <code> that Roslyn refused."""
        held = "<code>\nfirst\n\nsecond\n</code>"
        self.assertEqual(csharp_docs.paragraphs(held), [held])

    def test_a_break_never_lands_inside_a_tag(self):
        long = " ".join(["word"] * 12) + ' <see cref="NetwClockHandle.Tick"/>'
        for line in csharp_docs.wrap(long, "    "):
            self.assertNotIn("<see\n", line)
            self.assertFalse(line.rstrip().endswith("<see"))

    def test_a_list_row_keeps_its_own_line(self):
        lines = csharp_docs.wrap("- first row\n- second row", "")
        self.assertIn("/// - first row", lines)
        self.assertIn("/// - second row", lines)

    def test_no_prose_emits_no_comment(self):
        self.assertEqual(csharp_docs.block("    ", ""), [])


if __name__ == "__main__":
    unittest.main()
