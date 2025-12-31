# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

import os
import subprocess
from pathlib import Path

# -- Path setup --------------------------------------------------------------

MICROUI_BASE = Path(__file__).resolve().parents[1]
MICROUI_BUILD = Path(os.environ.get("DOC_BUILD_DIR", MICROUI_BASE / "doc" / "_build")).resolve()

# -- Project information -----------------------------------------------------

project = "MicroUI for Zephyr"
copyright = "2025, Fabian Blatz and contributors"
author = "Fabian Blatz"

# Try to get version from git tag, fallback to "dev"
try:
    version = subprocess.check_output(
        ["git", "describe", "--tags", "--abbrev=0"],
        cwd=MICROUI_BASE,
        stderr=subprocess.DEVNULL
    ).decode().strip().lstrip("v")
except (subprocess.CalledProcessError, FileNotFoundError):
    version = os.environ.get("VERSION", "dev")

release = version

# -- General configuration ---------------------------------------------------

extensions = [
    "breathe",
    "sphinx.ext.autodoc",
    "sphinx.ext.intersphinx",
    "sphinx_copybutton",
    "sphinx_tabs.tabs",
    "sphinxcontrib.jquery",
]

templates_path = ["_templates"]
exclude_patterns = ["_build", "_build_sphinx", "_build_doxygen", "Thumbs.db", ".DS_Store"]

# The root document
root_doc = "index"

# Pygments syntax highlighting
pygments_style = "sphinx"

# -- Options for HTML output -------------------------------------------------

html_theme = "sphinx_rtd_theme"
html_title = f"{project} {version}"

# Theme options for RTD
html_theme_options = {
    "logo_only": False,
    "prev_next_buttons_location": "bottom",
    "style_external_links": True,
    "collapse_navigation": False,
    "sticky_navigation": True,
    "navigation_depth": 4,
    "includehidden": True,
    "titles_only": False,
}

html_static_path = ["_static"]
html_css_files = ["custom.css"]

html_show_sourcelink = False
html_show_sphinx = False
html_last_updated_fmt = "%b %d, %Y"

# -- Options for Breathe (Doxygen integration) -------------------------------

breathe_projects = {
    "microui": str(MICROUI_BUILD / "doxygen" / "xml"),
}
breathe_default_project = "microui"
breathe_default_members = ("members", "undoc-members")

# -- Options for Intersphinx -------------------------------------------------

intersphinx_mapping = {
    "zephyr": ("https://docs.zephyrproject.org/latest/", None),
}

# -- Options for copybutton --------------------------------------------------

copybutton_prompt_text = r"\$ |uart:~\$ "
copybutton_prompt_is_regexp = True

# -- Custom setup ------------------------------------------------------------

def setup(app):
    # Add any custom setup here
    pass
