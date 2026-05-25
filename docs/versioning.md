# Versioning

Orion uses [Semantic Versioning](https://semver.org): `MAJOR.MINOR.PATCH`

| Part    | When to increment                          | Example                     |
|---------|--------------------------------------------|-----------------------------|
| `PATCH` | Bug fix, no API change                     | `0.0.0` → `0.0.1`          |
| `MINOR` | New feature, backwards compatible          | `0.0.0` → `0.1.0`          |
| `MAJOR` | Breaking change to a public API            | `0.x.x` → `1.0.0`          |

## Release cadence — milestone-gated

Releases are cut when a GitHub milestone closes. Each milestone maps to one
`MINOR` version increment. Bug fixes between milestones get `PATCH` releases.

| Milestone | Target version | Trigger |
|---|---|---|
| M1 — Core Runtime | `v0.1.0` | All M1 issues closed: `FrameScheduler`, `SimClock` Phase 1 |
| M2 — Observability | `v0.2.0` | All M2 issues closed: `CrashHandler`, `LoggerFactory`, `HealthPublisher` |
| M3 — Simulation | `v0.3.0` | All M3 issues closed: `ExternalClock` stub, `orion_sim_clock` target |
| M4 — External Interfaces | `v0.4.0` | Edge network and public API services shipped |
| First stable release | `v1.0.0` | Public API frozen, all services deployable to production hardware |

**Pre-1.0 contract:** `MINOR` increments may include breaking changes to
internal library APIs. The public API surface does not exist until `v1.0.0`.
External integrators should not pin below `v1.0.0`.

**`PATCH` releases:** Cut a patch release (`v0.1.1`, `v0.1.2`, ...) for bug
fixes that should not wait for the next milestone. All `fix:` commits since the
last tag appear in the changelog automatically.

## How it works

The version is sourced entirely from git tags. There is no version number to edit in any file — the tag *is* the release.

At configure time, CMake runs `git describe --tags --abbrev=0` twice — once in `CMakeLists.txt` to set `project(VERSION ...)`, and once in `cmake/tools.cmake` to set `ORION_VERSION`. The `docs` CMake target passes `ORION_VERSION` as the `ORION_PROJECT_VERSION` environment variable when invoking Doxygen. For a tag `v0.0.0`, Doxygen receives `v0.0.0` and the CMake project version is `0.0.0`.

## Creating a release

1. Ensure the working tree is clean and all changes are committed
2. Create an annotated tag:
   ```bash
   git tag -a v0.2.0 -m "Release 0.2.0"
   ```
3. Push the tag:
   ```bash
   git push origin v0.2.0
   ```
4. Re-run CMake configure to pick up the new version:
   ```bash
   cmake --preset=debug
   ```

Pushing the tag automatically triggers the **Release** workflow
(`.github/workflows/changelog.yml`), which:

1. Runs `git-cliff` with `cliff.toml` to generate a changelog for the new tag from all
   conventional commits since the previous tag
2. Creates a GitHub Release with the generated changelog as the release body

The changelog is derived entirely from commit messages. This is why all commits must follow
[Conventional Commits](https://www.conventionalcommits.org) — each `feat:` and `fix:` becomes
a line in the release notes automatically.

## CHANGELOG format

`cliff.toml` groups commits into sections by type:

| Commit type | Changelog section |
|---|---|
| `feat` | Features |
| `fix` | Bug Fixes |
| `perf` | Performance |
| `refactor` | Refactoring |
| `docs` | Documentation |
| `test` | Testing |
| `ci` | CI/CD |
| `build` | Build System |
| `chore` | Miscellaneous |
| `style` | (skipped) |

Scopes are rendered in bold: `**transport**: add subscriber reconnection`.

To preview the changelog for the current state of `main` without creating a release:

```bash
pip install git-cliff  # or: cargo install git-cliff
git-cliff --current --strip header
```

## During development (no tag yet)

If no tag exists on the current branch:

- `CMakeLists.txt` defaults to `0.0.0` and prints:
  ```
  -- No version tag found — defaulting to 0.0.0
  ```
- `cmake/tools.cmake` defaults `ORION_VERSION` to `"dev"`, so Doxygen will show `dev` as the version.

This is expected on feature branches or a fresh clone before any release has been tagged.

## Annotated vs lightweight tags

Always use annotated tags (`-a` flag) for releases. Annotated tags store a message, a date, and the tagger — they are proper release objects in git history. Lightweight tags are just pointers to a commit and carry no metadata.
