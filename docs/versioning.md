# Versioning

Orion uses [Semantic Versioning](https://semver.org): `MAJOR.MINOR.PATCH`

| Part    | When to increment                          | Example                     |
|---------|--------------------------------------------|-----------------------------|
| `PATCH` | Bug fix, no API change                     | `0.1.0` → `0.1.1`          |
| `MINOR` | New feature, backwards compatible          | `0.1.0` → `0.2.0`          |
| `MAJOR` | Breaking change to a public API            | `0.x.x` → `1.0.0`          |

## How it works

The version is sourced entirely from git tags. There is no version number to edit in any file — the tag *is* the release.

At configure time, CMake runs `git describe --tags --abbrev=0` to find the nearest tag and sets `PROJECT_VERSION` from it. This flows automatically into Doxygen and any other tooling that uses `@PROJECT_VERSION@`.

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
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
   ```

## During development (no tag yet)

If no tag exists on the current branch, CMake defaults to `0.0.0` and prints:

```
-- No version tag found — defaulting to 0.0.0
```

This is expected on feature branches or a fresh clone before any release has been tagged.

## Annotated vs lightweight tags

Always use annotated tags (`-a` flag) for releases. Annotated tags store a message, a date, and the tagger — they are proper release objects in git history. Lightweight tags are just pointers to a commit and carry no metadata.
