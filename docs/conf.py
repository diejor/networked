# Configuration file for the Sphinx documentation builder.

import os
import sys

sys.path.insert(0, os.path.abspath("_extensions"))

project = "Networked"
copyright = "2026 Diego Rodrigues. Base documentation structure and theme adapted from the Godot Engine (© 2014-present Juan Linietsky, Ariel Manzur and the Godot community) licensed under CC BY 3.0."
author = "Diego Rodrigues"

extensions = [
    "gdscript",
]

html_theme = "sphinx_rtd_theme"
html_static_path = ["_static"]
html_css_files = ["css/custom.css"]
html_js_files = ["js/custom.js"]

html_theme_options = {
    "collapse_navigation": False,
    "flyout_display": "attached",
    "prev_next_buttons_location": None,
}

exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

master_doc = "index"
source_suffix = ".rst"

language = "en"

html_show_sourcelink = False
html_copy_source = False
html_use_index = False

pygments_style = "sphinx"
highlight_language = "gdscript"
