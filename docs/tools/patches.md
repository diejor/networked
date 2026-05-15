# make_rst.py patches

This file tracks all modifications made to the Godot engine's `doc/tools/make_rst.py` for the
Networked addon documentation build.

## Patch 1: Import path fix
**Lines**: ~14
**Rationale**: In Godot engine, `make_rst.py` lives in `doc/tools/` and imports from the engine
root (`../../`). In our addon, it lives in `docs/tools/` and must import from `docs/` (`../`).
Also added `os.path.normpath()` for platform safety.

```diff
- sys.path.insert(0, root_directory := os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../"))
+ sys.path.insert(0, root_directory := os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../")))
```

## Patch 2: @GlobalScope guard
**Lines**: ~2206
**Rationale**: The constant resolution logic appends `state.classes["@GlobalScope"]` unconditionally,
but our addon XML set does not include `@GlobalScope`. Added existence check to prevent KeyError.

```diff
- search_class_defs.append(state.classes["@GlobalScope"])
+ if "@GlobalScope" in state.classes:
+     search_class_defs.append(state.classes["@GlobalScope"])
```

## Patch 3: External links for unresolved Godot types
**Lines**: ~1489-1494
**Rationale**: Our addon XML set contains ~127 classes. Any Godot engine type referenced
(e.g. `Node`, `Resource`, `Variant`) is not in `state.classes`. Instead of printing an error
and rendering as plain ``type``, generate a clickable external link to docs.godotengine.org.

```diff
  def resolve_type(link_type: str) -> str:
      if link_type in state.classes:
          return f":ref:`{link_type}<class_{sanitize_class_name(link_type)}>`"
      else:
-         print_error(f'... Unresolved type "{link_type}".', state)
-         return f"``{link_type}``"
+         slug = sanitize_class_name(link_type).lower()
+         url = f"https://docs.godotengine.org/en/stable/classes/class_{slug}.html"
+         return f"`{link_type} <{url}>`__"
```

## Patch 4: Indentation fix (pre-applied in source copy)
**Lines**: ~2204-2225
**Rationale**: The `if link_target.find(".") == -1:` block for constant cross-references was
at the wrong indentation level (elif level instead of inside the constant elif block).
Our source copy already had this fix applied.

## Patch 5: Array/Dictionary use resolve_type()
**Lines**: ~1496-1503
**Rationale**: `Array` and `Dictionary` were hardcoded as `:ref:`Array<class_Array>``
and `:ref:`Dictionary<class_Dictionary>``. Since `Array` and `Dictionary` are not in our
XML set, these produce broken Sphinx links. Using `resolve_type()` makes them generate
external links to docs.godotengine.org.

```diff
- return f":ref:`Array<class_Array>`\\[{resolve_type(...)}\\]"
+ return f"{resolve_type('Array')}\\[{resolve_type(...)}\\]"

- return f":ref:`Dictionary<class_Dictionary>`\\[{resolve_type(...)}, {resolve_type(...)}\\]"
+ return f"{resolve_type('Dictionary')}\\[{resolve_type(...)}, {resolve_type(...)}\\]"
```

## Patch 6: Cross-reference tags → external links
**Lines**: ~2136-2280
**Rationale**: GDScript documentation uses cross-reference tags like
`[constant Node.NOTIFICATION_PARENTED]` or `[method Object.something]`. When the target
class is not in our XML set (e.g. Godot engine classes), the original code generates a
broken `:ref:` to a non-existent RST anchor. We now generate an external link to
docs.godotengine.org with the correct HTML fragment anchor. Errors are downgraded to warnings
since unresolved references are expected for a standalone addon.

External link pattern:
```
https://docs.godotengine.org/en/stable/classes/class_{slug}.html#class_{Class}_{reftype}_{targetname}
```

## Patch 7: |abstract| substitution
**Lines**: ~1670-1685
**Rationale**: GDScript 2.0 methods with `qualifiers="abstract"` in XML produce `|abstract|`
substitution references in RST. The original `make_footer()` defines substitutions for
`|virtual|`, `|required|`, `|const|`, `|vararg|`, `|static|`, `|operator|`, `|bitfield|`,
`|void|` but not `|abstract|`. Added the missing substitution.

```diff
+ abstract_msg = translate("This method is abstract and has no implementation. It must be overridden by subclasses.")
   ...
+ f".. |abstract| replace:: :abbr:`abstract ({abstract_msg})`\n"
```

## Patch 8: Guard against empty post_text in linebreak handling
**Lines**: ~2360
**Rationale**: `while post_text[0] == " ":` can crash with `IndexError` when `post_text`
is an empty string (e.g., when a `[br]` tag appears at the end of a description).
Added `len(post_text) > 0` guard.

```diff
- while post_text[0] == " ":
+ while len(post_text) > 0 and post_text[0] == " ":
```

## Patch 9: Fix Patch 6 indentation — missing outer else for unknown target classes
**Lines**: ~2247
**Rationale**: Patch 6's `else` block for `target_class_name not in state.classes` was
accidentally placed at the wrong indentation level (inside the `if target_class_name in
state.classes:` block, acting as an else for the `if tag_state.name == ...` chain). Added
a correctly-indented `else:` paired with the outer `if`. Without this fix, external links
for `[constant Node.NOTIFICATION_PARENTED]`-style tags were not generated.

```diff
                          # inside if target_class_name in state.classes:
                              else:
                                  print_warning(...)
                                  resolved = False
+ 
+                         else:  # target_class_name not in state.classes
+                             print_warning(...)
+                             resolved = False

## Patch 10: Code block paragraph-eating fix
**Lines**: preformat_text_block()
**Rationale**: After a `[/codeblock]` tag, the next line in the XML description has leading whitespace
(tabs/spaces from XML indentation). In RST, a leading space after a `::` literal block makes docutils
treat the next paragraph as block continuation — swallowing it into `<pre>`. The official godot-docs
RST escapes this by placing `\ ` (backslash-space) before the text. Our older `make_rst.py` copy
did not have this logic.

Added a `just_exited_codeblock` flag that is set when `codeblock_tag` becomes empty (`""`).
When `True`, the next line's leading whitespace is stripped and replaced with a `\ ` RST escape.

```diff
      just_exited_codeblock = False
      codeblock_tag = ""
      codeblock_lines: list[str] = []
      ...
              else:
                  codeblock_tag = ""
+                 just_exited_codeblock = True
+     ...
+     if just_exited_codeblock:
+         line = line[len(cb_start) :]
+         line = f"\\ {line}"
+         just_exited_codeblock = False
```

## Patch 11: Godot engine class group fallback for inheritance resolution
**Lines**: ~116 (new dict), ~644-647 (fallback logic)
**Rationale**: `update_class_group()` determines a class's category (node/resource/object/variant)
by walking the `inherits` chain through `state.classes`. Since our XML set only contains the addon's
own classes (not Godot engine base classes like `Node`, `Resource`, `Object`, `Control`), the walk
exits immediately when it hits an engine class — defaulting everything to `"variant"`.

Added a `GODOT_ENGINE_CLASS_GROUPS` lookup table mapping ~25 well-known Godot engine class names to
their ultimate group. After the `while inherits in state.classes` loop fails (engine class not found),
the fallback checks the table to assign the correct group.

```diff
+ # After CLASS_GROUPS_BASE:
+ GODOT_ENGINE_CLASS_GROUPS: dict[str, str] = {
+     "Node": "node",
+     "Control": "node",
+     "Area2D": "node",
+     ...
+     "Resource": "resource",
+     "ResourceFormatLoader": "resource",
+     ...
+     "Object": "object",
+     "RefCounted": "object",
+     "MultiplayerPeerExtension": "object",
+ }

  def update_class_group(self, state: State) -> None:
      ...
+         # Fallback: if the walk exited without finding a group
+         # (because the parent is a Godot engine class not in our XML set),
+         # check against the known Godot engine class hierarchy.
+         if group_name == "variant" and inherits in GODOT_ENGINE_CLASS_GROUPS:
+             group_name = GODOT_ENGINE_CLASS_GROUPS[inherits]
```

## Patch 12: Do not emit missing Godot base classes in grouped index
**Lines**: ~1796
**Rationale**: Godot's generated class index inserts each group's base class first (`Node`,
`Resource`, `Object`, `Variant`) because the full engine XML set includes pages for those classes.
Our standalone addon docs do not generate `class_node.rst`, `class_resource.rst`, or
`class_object.rst`, so inserting those base links creates Sphinx toctree warnings.

Changed the base-class insertion to only emit the base class when it exists in the current grouped
class list.

```diff
- if group_name in CLASS_GROUPS_BASE:
+ if group_name in CLASS_GROUPS_BASE and CLASS_GROUPS_BASE[group_name] in grouped_classes[group_name]:
      f.write(f"    class_{sanitize_class_name(CLASS_GROUPS_BASE[group_name], True)}\n")
```

## Patch 13: External links for unresolved enums
**Rationale**: Like Patch 3, `make_enum()` would error on engine enums like `Error`. Added a fallback to link to Godot's online documentation.

```diff
-     print_error(f'{state.current_class}.xml: Unresolved enum "{t}".', state)
-     return t
+     # Fallback for Godot engine enums: link to external docs.
+     print_warning(f'{state.current_class}.xml: Unresolved enum "{t}". Linking to Godot docs.', state)
+     slug = sanitize_class_name(c).lower()
+     url = f"https://docs.godotengine.org/en/stable/classes/class_{slug}.html#enum_{slug}_{e}"
+     return f"`{t} <{url}>`__"
```

## Patch 14: Recognize unknown class tags as external links
**Rationale**: Godot descriptions often use `[Node]` or `[Dictionary]` tags. If the class isn't in our local XML set, `make_rst.py` would fail with an "Unrecognized opening tag" error. Added logic to treat uppercase tags as engine class references.

```diff
-         if tag_text in state.classes and not inside_code:
+         if (tag_text in state.classes or (tag_text and tag_text[0].isupper() and "=" not in tag_text and tag_text not in RESERVED_FORMATTING_TAGS)) and not inside_code:
```

## Patch 15: Ignore [color] and [font] tags
**Rationale**: Godot's XML sometimes includes BBCode-style `[color]` or `[font]` tags which are not supported by RST. Added empty handlers to prevent "Unrecognized opening tag" errors.

```diff
+             elif is_in_tagset(tag_state.name, ["color", "font"]):
+                 if tag_state.closing:
+                     tag_depth -= 1
+                     debug_tag_stack.pop()
+                 else:
+                     tag_depth += 1
+                     debug_tag_stack.append(tag_state.name)
+                 tag_text = ""
```

## Patch 16: Downgrade argument reference errors to warnings
**Rationale**: Standalone builds can sometimes lose context for argument references (e.g. `[param address]`). Downgraded from `print_error` to `print_warning` to prevent build failures.

```diff
-                                 print_error(f'{state.current_class}.xml: Unresolved argument reference "{link_target}" in {context_name}.', state)
+                                 print_warning(f'{state.current_class}.xml: Unresolved argument reference "{link_target}" in {context_name}.', state)
```
