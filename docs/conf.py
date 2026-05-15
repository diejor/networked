# Configuration file for the Sphinx documentation builder.
# https://www.sphinx-doc.org/en/master/usage/configuration.html

import os
import sys

sys.path.insert(0, os.path.abspath("_extensions"))

# -- Project info ----------------------------------------------------------

project = "Networked"
copyright = (
    "2026 Diego Rodrigues. Base documentation structure and theme adapted "
    "from the Godot Engine (© 2014-present Juan Linietsky, Ariel Manzur "
    "and the Godot community) licensed under CC BY 3.0"
)
author = "Diego Rodrigues"

# Version comes from RTD when building there, otherwise falls back to "latest".
version = os.getenv("READTHEDOCS_VERSION", "latest")
release = version

# -- General configuration -------------------------------------------------

needs_sphinx = "8.1"

extensions = [
    "gdscript",
    "sphinx_copybutton",
    "notfound.extension",
    "sphinxext.opengraph",
]

templates_path = ["_templates"]
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

master_doc = "index"
source_suffix = ".rst"
source_encoding = "utf-8-sig"

language = "en"

smartquotes = False
pygments_style = "sphinx"
highlight_language = "gdscript"

# Useful UI roles ported from godot-docs prose conventions. Define them here
# so future tutorial/manual pages can use :kbd:, :button:, :menu: etc. without
# extra setup.
rst_prolog = """
.. role:: button
   :class: role-button role-ui

.. role:: menu
   :class: role-menu role-ui

.. role:: kbd
   :class: kbd

"""

# -- Environment-aware config ----------------------------------------------

on_rtd = os.environ.get("READTHEDOCS", None) == "True"

# Don't add `/en/latest` prefix during local development so the local 404
# page renders correctly.
if not on_rtd:
    notfound_urls_prefix = ""

# -- HTML output -----------------------------------------------------------

html_theme = "sphinx_rtd_theme"
html_static_path = ["_static"]
html_css_files = ["css/custom.css"]
html_js_files = ["js/custom.js"]

html_theme_options = {
    "collapse_navigation": False,
    "flyout_display": "attached",
    "prev_next_buttons_location": "bottom",
    "logo_only": False,
    "version_selector": True,
    "language_selector": False,
}

html_title = f"Networked {version} documentation"

# Edit-on-GitHub link in the RTD theme. Update github_version when cutting
# release branches.
html_context = {
    "display_github": True,
    "github_user": "diejor",
    "github_repo": "networked",
    "github_version": "main",
    "conf_py_path": "/docs/",
    # Giscus toggles, read by _templates/layout.html.
    "show_comments": True,
}

html_show_sourcelink = False
html_copy_source = False
html_use_index = False

# -- OpenGraph -------------------------------------------------------------

ogp_site_name = "Networked documentation"
ogp_social_cards = {"enable": False}

# -- linkcheck -------------------------------------------------------------

linkcheck_anchors = False
linkcheck_timeout = 10
linkcheck_ignore = [
    # Anchored Godot class links are generated heuristically and are
    # impossible to validate without a full HTML head fetch.
    r"https://docs\.godotengine\.org/.*#.*",
]
