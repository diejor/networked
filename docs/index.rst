:github_url: hide

Networked
=========

Networked is a proof-of-concept multiplayer framework for Godot 4. It builds
on Godot's :godot:`SceneMultiplayer <SceneMultiplayer>` API and provides a
smaller set of nodes and resources for common multiplayer projects.

Use Networked when you want to host or join a session, spawn players into
scenes, synchronize movement, move players between levels, or save player
state without writing the same session plumbing from scratch.

Features
--------

- Scene-aware player spawning with :ref:`MultiplayerSceneManager <class_MultiplayerSceneManager>`
  and :ref:`SpawnerComponent <class_SpawnerComponent>`.
- Smooth snapshot playback with :ref:`TickInterpolator <class_TickInterpolator>`.
- Level transitions with :ref:`TPComponent <class_TPComponent>`.
- Player data persistence with :ref:`NetwDatabase <class_NetwDatabase>` and
  :ref:`SaveComponent <class_SaveComponent>`.
- Transport resources for ENet, WebSocket, WebRTC, Steam, and local loopback.
- Debug logging and topology tools for following what happens across peers.

Start with the :ref:`quick start <doc_quick_start>` if this is your first
Networked project. The :ref:`manual <doc_manual_overview>` explains each
subsystem in more detail, and the :ref:`class reference <toc-class-ref>` lists
the full API.

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
