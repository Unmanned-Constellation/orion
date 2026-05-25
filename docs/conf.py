project = "Orion"
author = "Unmanned Constellation"

# Added 'exhale' to the extensions
extensions = [
    "breathe", 
    "myst_parser",
    "exhale"
]

breathe_projects = {"orion": "_build/doxygen/xml"}
breathe_default_project = "orion"

# Exhale configuration
exhale_args = {
    "containmentFolder":     "./api",
    "rootFileName":          "library_root.rst",
    "doxygenStripFromPath":  "..",
    "rootFileTitle":         "Orion API Reference",
    "createTreeView":        True,
    "exhaleExecutesDoxygen": False
}

html_theme = "furo"
exclude_patterns = ["_build", "requirements.txt"]