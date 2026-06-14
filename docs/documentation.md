# Documentation Pipeline

Orion uses a three-tool chain to produce its API reference:

```
C++ headers → Doxygen (XML) → Breathe → Sphinx (HTML)
```

- **Doxygen** parses doc comments in `.hpp` files and emits structured XML.
- **Breathe** is a Sphinx extension that reads that XML and exposes Doxygen
  entities as RST directives.
- **Sphinx** renders the final HTML site, combining the API reference with the
  hand-written Markdown guides in `docs/`.

## Tool configuration

### Doxygen (`docs/Doxyfile`)

| Setting | Value | Purpose |
|---|---|---|
| `INPUT` | `./libs ./proto` | Parse public headers and proto stubs |
| `FILE_PATTERNS` | `*.hpp` | C++ headers only |
| `EXCLUDE_PATTERNS` | `*/build/* *_impl.hpp` | Skip generated artefacts and PIMPL headers |
| `GENERATE_XML` | `YES` | Breathe consumes XML, not HTML |
| `GENERATE_HTML` | `NO` | Sphinx produces the HTML instead |
| `WARN_AS_ERROR` | `FAIL_ON_WARNINGS` | Undocumented public symbols fail CI |
| `WARN_IF_UNDOCUMENTED` | `YES` | Every public declaration needs a doc comment |
| `OUTPUT_DIRECTORY` | `./docs/_build/doxygen` | XML lands in `docs/_build/doxygen/xml/` |

`PROJECT_NUMBER` is set from the `ORION_PROJECT_VERSION` environment variable,
which CMake captures from `git describe --tags --abbrev=0` at configure time.

### Sphinx (`docs/conf.py`)

| Setting | Value |
|---|---|
| Extensions | `breathe`, `myst_parser`, `exhale`, `sphinxcontrib.mermaid` |
| Theme | `furo` |
| `breathe_projects["orion"]` | `_build/doxygen/xml` |
| `breathe_default_project` | `orion` |

`myst_parser` allows the Markdown guides (`.md`) in `docs/` to be included in
the Sphinx toctree alongside `.rst` files. `exhale` auto-generates the
`docs/api/` tree from the Doxygen XML — that directory is gitignored and
regenerated on every build. `sphinxcontrib.mermaid` renders `{mermaid}` fenced
blocks in Markdown as SVG diagrams.

### Python dependencies (`docs/requirements.txt`)

```
sphinx>=7.0
breathe>=4.35
furo>=2024.1
myst-parser>=3.0
```

Install them before running Sphinx:

```bash
pip install -r docs/requirements.txt
```

## Running locally

The two-step sequence mirrors what CI does:

```bash
# Step 1: generate Doxygen XML
doxygen docs/Doxyfile

# Step 2: build the Sphinx site
sphinx-build -W -b html docs docs/_build/html
```

`-W` promotes Sphinx warnings to errors, consistent with CI. Open
`docs/_build/html/index.html` in a browser to review the output.

Both output directories are gitignored (`docs/_build/`).

The **Docs: Build** VS Code task runs the full pipeline (Doxygen then Sphinx) but does not
set `ORION_PROJECT_VERSION`. To include the correct version string in the generated pages,
set the variable before running Doxygen manually:

```bash
export ORION_PROJECT_VERSION=$(git describe --tags --abbrev=0)
doxygen docs/Doxyfile
sphinx-build -W -b html docs docs/_build/html
```

## Documenting C++ with Doxygen

Use triple-slash `///` or Javadoc `/** */` block comments on public declarations:

```cpp
/// Brief one-line description.
///
/// @param config Session configuration including vehicle_id and service_name.
/// @return       A Session connected to the Zenoh bus, or throws on failure.
static auto create(SessionConfig config) -> Session;
```

Key rules enforced by the Doxyfile:

- `WARN_IF_UNDOCUMENTED = YES` - every public class, function, and field needs
  at minimum a brief description.
- `WARN_NO_PARAMDOC = YES` - every parameter must have a `@param` annotation.
- `WARN_AS_ERROR = FAIL_ON_WARNINGS` - missing docs fail the CI `docs` job.
- `EXCLUDE_PATTERNS = *_impl.hpp` - PIMPL implementation headers are excluded;
  document the public interface headers only.

## API reference generation

`exhale` reads the Doxygen XML and auto-generates one `.rst` file per
namespace, class, struct, function, and file under `docs/api/`. The root page
is `docs/api/library_root.rst`, linked from the `index.md` toctree.

You can also embed individual symbols inline in any Markdown guide using
Breathe directives:

```rst
.. doxygenclass:: orion::transport::Session
   :project: orion
   :members:
```

Available directives: `doxygennamespace`, `doxygenclass`, `doxygenstruct`,
`doxygenfunction`, `doxygentypedef`. See the
[Breathe directive reference](https://breathe.readthedocs.io/en/latest/directives.html)
for the full list.

## CI enforcement

The `docs` CI job runs on every push and PR, in parallel with the `format` and
`commitlint` jobs:

```
doxygen docs/Doxyfile
sphinx-build -W -b html docs docs/_build/html
```

All downstream build, test, sanitizer, coverage, and fuzz jobs have `docs` as a
prerequisite (`needs: [format, commitlint, docs, proto]`). A docs failure blocks the
entire pipeline.

What fails the docs job:

- A public symbol in `libs/` or `proto/` is missing its doc comment or `@param`
- A Sphinx page references a symbol or file that does not exist
- A Markdown guide is structurally invalid (bad heading hierarchy, broken link)
- Any Sphinx or Doxygen warning

See [ci-cd.md](ci-cd.md) for the full job list and merge requirements.
