# Makefile for MicroUI documentation
#
# This Makefile builds both Doxygen and Sphinx documentation.
# Run from the doc/ directory.

# Configuration
SPHINXOPTS    ?=
SPHINXBUILD   ?= sphinx-build
DOXYGEN       ?= doxygen
SOURCEDIR     = .
BUILDDIR      = _build
PROJECTROOT   = ..

# Default target
.PHONY: help
help:
	@echo "MicroUI Documentation Build"
	@echo ""
	@echo "Available targets:"
	@echo "  html      - Build HTML documentation (Doxygen + Sphinx)"
	@echo "  doxygen   - Build Doxygen XML/HTML only"
	@echo "  sphinx    - Build Sphinx documentation only (requires doxygen first)"
	@echo "  clean     - Remove build artifacts"
	@echo "  livehtml  - Start live reload server for development"
	@echo ""

# Build everything
.PHONY: html
html: doxygen sphinx
	@echo "Documentation built successfully in $(BUILDDIR)/html"

# Build Doxygen documentation
.PHONY: doxygen
doxygen:
	@echo "Building Doxygen documentation..."
	@mkdir -p $(BUILDDIR)/doxygen
	cd $(PROJECTROOT) && $(DOXYGEN) doc/Doxyfile

# Build Sphinx documentation
.PHONY: sphinx
sphinx:
	@echo "Building Sphinx documentation..."
	@mkdir -p $(BUILDDIR)/html
	DOC_BUILD_DIR=$(CURDIR)/$(BUILDDIR) $(SPHINXBUILD) -b html "$(SOURCEDIR)" "$(BUILDDIR)/html" $(SPHINXOPTS)

# Clean build artifacts
.PHONY: clean
clean:
	rm -rf $(BUILDDIR)
	@echo "Build directory cleaned"

# Live reload server for development
.PHONY: livehtml
livehtml: doxygen
	@echo "Starting live reload server..."
	DOC_BUILD_DIR=$(CURDIR)/$(BUILDDIR) sphinx-autobuild "$(SOURCEDIR)" "$(BUILDDIR)/html" $(SPHINXOPTS)
