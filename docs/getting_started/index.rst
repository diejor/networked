:allow_comments: False

.. _doc_getting_started_intro:

Getting started
===============

This section is the entry point for new Networked users. It assumes you can
already build a small single-player Godot project but have not yet built a
multiplayer one. By the end of the quick start you will have a window that
hosts a session, accepts another instance as a client, and replicates a
moving character between them.

If you have used Godot's :godot:`SceneMultiplayer <SceneMultiplayer>`
directly before, you will recognize the pieces underneath. Networked never
hides Godot's high-level API, it just wraps it so the most common project
shape (host, scene, players, transport) takes a handful of inspector clicks
instead of a few hundred lines of glue code.

.. toctree::
   :maxdepth: 1
   :name: toc-getting-started

   quick_start
