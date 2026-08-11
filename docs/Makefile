# Minimal Sphinx build helper for Linux/macOS.
#
# Usage:
#     make html      Build the HTML docs into _build/html
#     make api       Regenerate classes/ from addon GDScript via godot --doctool
#     make live      Live-reload server on http://127.0.0.1:8000
#     make linkcheck Verify external links
#     make clean     Remove the _build directory

SPHINXOPTS    ?=
SPHINXBUILD   ?= sphinx-build
SOURCEDIR     := .
BUILDDIR      := _build

.PHONY: help html api live linkcheck clean

help:
	@echo "Targets: html, api, live, linkcheck, clean"

api:
	@command -v godot >/dev/null 2>&1 || { \
		echo "godot is not in your PATH. Install Godot 4.6+ and add it to PATH,"; \
		echo "or see .github/actions/build-docs-classes/action.yml for the CI flow."; \
		exit 1; \
	}
	rm -rf api_new api_filtered_new classes_new
	@mkdir -p api_new
	@echo "[1/4] Extracting GDScript class XML via godot --doctool..."
	# Run from the project root with no --path: the --gdscript-docs argument
	# doubles as the project path, and adding --path on top breaks res://
	# loading (every script load fails and the doctool crashes). The unscoped
	# walk also collects vendor and engine-tree scripts; the filter step cuts
	# them.
	cd .. && godot --doctool "$(CURDIR)/api_new" --gdscript-docs . --headless --quit
	@echo "[2/4] Copying extension class XML from doc_classes..."
	cp ../extension/doc_classes/*.xml api_new/
	@echo "[3/4] Filtering private members..."
	python tools/filter_private.py api_new api_filtered_new
	@echo "[4/4] Generating RST into classes/..."
	python tools/make_rst.py api_filtered_new --output classes_new
	rm -rf api api_filtered classes
	mv api_new api
	mv api_filtered_new api_filtered
	mv classes_new classes
	@echo
	@echo "Class reference updated. Run 'make html' next."

html:
	$(SPHINXBUILD) -b html $(SOURCEDIR) $(BUILDDIR)/html $(SPHINXOPTS)
	@echo
	@echo "Build finished. Open $(BUILDDIR)/html/index.html."

live:
	@command -v sphinx-autobuild >/dev/null 2>&1 || { \
		echo "sphinx-autobuild is not installed. Install with:"; \
		echo "    pip install sphinx-autobuild"; \
		exit 1; \
	}
	sphinx-autobuild -b html $(SOURCEDIR) $(BUILDDIR)/html $(SPHINXOPTS)

linkcheck:
	$(SPHINXBUILD) -b linkcheck $(SOURCEDIR) $(BUILDDIR)/linkcheck $(SPHINXOPTS)

clean:
	rm -rf $(BUILDDIR)
	@echo "Removed $(BUILDDIR)."
