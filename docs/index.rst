:github_url: hide

Networked
=========

Welcome to the Networked documentation. Networked is a small framework that
wraps Godot's :godot:`SceneMultiplayer <SceneMultiplayer>` into an opinionated
shape: a single :ref:`MultiplayerTree <class_MultiplayerTree>` node hosts or
joins a session, a :ref:`MultiplayerSceneManager <class_MultiplayerSceneManager>`
replicates levels, and :ref:`SpawnerComponent <class_SpawnerComponent>` decides
how players and entities enter the world.

The :ref:`quick start <doc_quick_start>` is the right entry point if this is
your first time using the addon. From there, the :ref:`manual
<doc_manual_overview>` covers each subsystem in depth, and the
:ref:`class reference <toc-class-ref>` documents every exported method,
member, and signal.

.. toctree::
   :maxdepth: 1
   :caption: Getting started

   getting_started/index

.. toctree::
   :maxdepth: 1
   :caption: Manual

   manual/index

.. toctree::
   :maxdepth: 1
   :caption: Contributing

   contributing/index

.. toctree::
   :maxdepth: 2
   :caption: Class reference
   :name: toc-class-ref

   classes/index
