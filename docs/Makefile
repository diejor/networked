# Minimal Sphinx build helper for Linux/macOS.
#
# Usage:
#     make html      Build the HTML docs into _build/html
#     make live      Live-reload server on http://127.0.0.1:8000
#     make linkcheck Verify external links
#     make clean     Remove the _build directory

SPHINXOPTS    ?=
SPHINXBUILD   ?= sphinx-build
SOURCEDIR     := .
BUILDDIR      := _build

.PHONY: help html live linkcheck clean

help:
	@echo "Targets: html, live, linkcheck, clean"

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
