.. _doc_contributing_documentation:

Writing documentation
=====================

Networked's documentation is written in reStructuredText and built with
`Sphinx <https://www.sphinx-doc.org/>`__ using the
`Read the Docs theme <https://sphinx-rtd-theme.readthedocs.io/>`__. The
class reference is generated from Godot's in-source XML by a vendored copy
of ``make_rst.py`` -- the same tool the engine itself uses -- with a small
stack of patches that make the output addon-friendly. This page describes
both the prose and reference pipelines so you can contribute either kind of
change confidently.

Project layout
--------------

The ``docs/`` directory mirrors the structure of godot-docs as closely as
it can:

- ``docs/getting_started/`` -- onboarding and the :ref:`quick start
  <doc_quick_start>`.
- ``docs/manual/`` -- reference-style prose for each subsystem.
- ``docs/contributing/`` -- this section.
- ``docs/classes/`` -- generated class reference. **Do not edit by hand**;
  see below.
- ``docs/_extensions/`` -- the Sphinx extensions that ship with the docs.
  The most important is ``godot_xref``, which defines the ``:godot:`` role
  used to link out to ``docs.godotengine.org``.
- ``docs/tools/`` -- the ``make_rst.py`` build, vendored upstream copy, and
  the patch series.

Building locally
----------------

Install the documentation dependencies into a fresh virtualenv. The
``docs/requirements.txt`` file pins every Sphinx extension the build uses
-- ``sphinx-tabs`` for the tabbed code blocks, ``sphinx-copybutton`` for
the "copy" affordance, and the OpenGraph and 404 helpers used in
production.

.. tabs::
 .. code-tab:: bash Linux/macOS

    python -m venv .venv
    source .venv/bin/activate
    pip install -r docs/requirements.txt
    pip install sphinx-autobuild   # optional, for `make live`

 .. code-tab:: powershell Windows

    python -m venv .venv
    .venv\Scripts\Activate.ps1
    pip install -r docs/requirements.txt
    pip install sphinx-autobuild   # optional, for `make live`

A ``Makefile`` and ``make.bat`` ship inside ``docs/`` for the common
targets. ``cd`` into the directory first, then:

.. tabs::
 .. code-tab:: bash Linux/macOS

    cd docs
    make html        # one-shot build into _build/html
    make live        # auto-rebuild + serve on http://127.0.0.1:8000
    make linkcheck   # validate external links
    make clean       # remove _build/

 .. code-tab:: powershell Windows

    cd docs
    .\make.bat html        # one-shot build into _build\html
    .\make.bat live        # auto-rebuild + serve on http://127.0.0.1:8000
    .\make.bat linkcheck   # validate external links
    .\make.bat clean       # remove _build\

Open ``docs/_build/html/index.html`` in a browser. The build is
incremental, so subsequent runs only re-render files you have touched.
``make live`` is the most productive option while editing prose -- the
server reloads the open browser tab the moment ``sphinx-build`` finishes.

To regenerate the class reference, first run ``make_rst.py`` against the
addon's XML output:

.. code-block:: console

    python docs/tools/make_rst.py docs/api -o docs/classes

The build will reject your PR if ``docs/classes/`` is out of sync with the
addon's XML; the CI runs the same command on push.

Writing prose pages
-------------------

The voice we target is the one godot-docs uses in its tutorials: warm,
paragraph-led, full of cross-references, with code blocks that show one
thing well. A few specific conventions are worth calling out.

Cross-references
~~~~~~~~~~~~~~~~

Three roles do most of the work:

- ``:ref:`label <doc_label>``` -- link to another page in this
  documentation. Every prose page declares a label at the top
  (``.. _doc_quick_start:``), so the link looks like
  ``:ref:`doc_quick_start```.
- ``:ref:`ClassName <class_ClassName>``` -- link to a class in *this*
  reference. The label format mirrors the ``class_*.rst`` files
  ``make_rst.py`` produces.
- ``:godot:`ClassName``` -- link to a class on the upstream Godot docs.
  The ``godot_xref`` extension constructs the URL automatically. Use this
  for engine classes like
  :godot:`SceneMultiplayer <SceneMultiplayer>`, never raw URLs.

If you need a fragment, pass it after a hash:
``:godot:`is_multiplayer_authority <Node#class_node_method_is_multiplayer_authority>```.
Use the slug Godot itself emits -- the extension does not invent fragments
for you.

Code samples
~~~~~~~~~~~~

Wrap GDScript snippets in ``.. tabs::`` blocks even when only GDScript is
available. It keeps the styling consistent with godot-docs and makes adding
a C# tab later a one-line edit:

.. code-block:: rst

    .. tabs::
     .. code-tab:: gdscript GDScript

        func _ready() -> void:
            print("hello")

Short inline names go in double backticks: ``connect_player()``,
``OFFLINE``. Class names always use the role form so they become links.

Tone
~~~~

- Lead with a paragraph that explains *why* the page exists, not just *what*
  it covers. The reader is impatient and you are competing with the search
  bar.
- Prefer "you" over "the user". The docs are a one-on-one conversation with
  whoever is reading them.
- Show, then explain. A short code block followed by two paragraphs that
  walk through it lands much better than four paragraphs of theory and a
  reference at the end.
- Use admonitions (``.. note::``, ``.. tip::``, ``.. warning::``) sparingly
  -- one per page is usually right, three is the absolute cap. Overusing
  them trains the reader to skip them.

Class reference patches
-----------------------

The addon's class reference is generated by a vendored copy of Godot's own
``make_rst.py`` (see ``docs/tools/vendor/UPSTREAM.md``), modified by a small
patch series in ``docs/tools/patches/``. The patches do the things upstream
``make_rst.py`` cannot, because upstream assumes it is being run against
the engine, not an addon:

- Resolve external references (``[ClassName]`` of an engine class) to the
  ``:godot:`` role so they link out to docs.godotengine.org.
- Strip standalone addon-specific paths and pseudo-classes that would
  otherwise produce broken links.
- Suppress noisy warnings about engine-side tags the addon does not use.

Each patch starts with a one-line summary and explains *why* in its
description block. If you need to add a sixth, the workflow is documented
in ``docs/tools/patches/README.md`` -- briefly: make your change against
``docs/tools/vendor/make_rst.py``, run ``regen_patches.py``, commit the
patches and the updated ``make_rst.py``.

.. warning::

    Do not edit ``docs/tools/make_rst.py`` directly. It is regenerated from
    the vendored copy and the patch stack, and direct edits will be
    overwritten the next time the build runs.

When upstream releases a new ``make_rst.py``, refresh the vendored copy
and re-apply the patches. Two of the existing patches are minor textual
fixes that may have already been merged upstream; if they apply cleanly,
keep them, and if upstream has incorporated them, drop them and update the
patch index.
