project = "Orion"
author = "Unmanned Constellation"
html_title = "Orion Docs"

extensions = [
    "breathe",
    "myst_parser",
    "exhale",
    "sphinxcontrib.mermaid",
]

myst_enable_extensions = [
    "colon_fence",
    "deflist",
    "tasklist",
]

breathe_projects = {"orion": "_build/doxygen/xml"}
breathe_default_project = "orion"

exhale_args = {
    "containmentFolder":     "./api",
    "rootFileName":          "library_root.rst",
    "doxygenStripFromPath":  "..",
    "rootFileTitle":         "Orion API Reference",
    "createTreeView":        True,
    "exhaleExecutesDoxygen": False,
}

html_theme = "furo"
exclude_patterns = ["_build", "requirements.txt"]

# The doxygen/ subdirectory is copied into the Sphinx output tree after the
# build completes, so MyST can't resolve links to it during the build.
suppress_warnings = ["myst.xref_missing"]
