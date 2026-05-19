:allow_comments: False

.. _doc_getting_started_intro:

Getting started
===============

This section is the entry point for new Networked users. It assumes you can
already build a small single-player Godot project, but have not yet built a
multiplayer one. The quick start walks through a session that hosts locally,
accepts another editor instance as a client, and replicates a moving
character between both windows.

If you have used Godot's :godot:`SceneMultiplayer <SceneMultiplayer>`
directly before, the pieces should look familiar. Networked keeps the same
peer and RPC model, but gives common project structure--host, scene, players,
and transport--a smaller API.

.. toctree::
   :maxdepth: 1
   :name: toc-getting-started

   quick_start
