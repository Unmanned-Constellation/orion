project = "Orion"
author = "Unmanned Constellation"
extensions = ["breathe", "myst_parser"]

breathe_projects = {"orion": "_build/doxygen/xml"}
breathe_default_project = "orion"

html_theme = "furo"
exclude_patterns = ["_build", "requirements.txt"]
