.. _doc_contributing_documentation:

Documentation
=============

Networked's documentation is written in reStructuredText and built with
Sphinx. The hand-written pages live in ``docs/``. The class reference is
generated from the addon's Godot XML output and should not be edited by hand.

Building locally
----------------

Install the documentation dependencies in a virtual environment:

.. tabs::
 .. code-tab:: bash Linux/macOS

    python -m venv .venv
    source .venv/bin/activate
    pip install -r docs/requirements.txt

 .. code-tab:: powershell Windows

    python -m venv .venv
    .venv\Scripts\Activate.ps1
    pip install -r docs\requirements.txt

Then build the docs:

.. tabs::
 .. code-tab:: bash Linux/macOS

    cd docs
    make html

 .. code-tab:: powershell Windows

    cd docs
    .\make.bat html

Open ``docs/_build/html/index.html`` after the build finishes. While editing,
install ``sphinx-autobuild`` and run ``make live`` from the ``docs/``
directory to rebuild on save.

Class reference
---------------

The generated class reference uses a vendored copy of Godot's ``make_rst.py``
with a small patch stack for addon documentation. The generated files live in
``docs/classes/``.

Regenerate the class reference after changing public scripts or doc comments:

.. code-block:: console

    cd docs
    make api

On Windows, use:

.. code-block:: powershell

    cd docs
    .\make.bat api

The vendored tool and patch workflow are documented in
``docs/tools/vendor/UPSTREAM.md`` and ``docs/tools/patches/README.md``. Edit
the vendored source and regenerate patches when changing the generator. Do not
edit ``docs/tools/make_rst.py`` directly.

Useful references
-----------------

Use these roles for links:

- ``:ref:`quick start <doc_quick_start>``` for another page in these docs.
- ``:ref:`MultiplayerTree <class_MultiplayerTree>``` for Networked classes.
- ``:godot:`SceneMultiplayer <SceneMultiplayer>``` for Godot API pages.

Keep docs changes close to the code they describe. If a behavior is still
experimental or only verified in examples, say that plainly.
