# Contributing

QMLScrubPlayer uses a lightweight Git-flow branch model.

## Branches

- `main`: stable release history only.
- `develop`: integration branch for the next release.
- `feature/<name>`: feature work branched from `develop`.
- `release/<version>`: release stabilization branched from `develop`.
- `hotfix/<name>`: urgent fixes branched from `main`.

## Flow

1. Start new work from `develop`.
2. Merge completed feature branches back into `develop`.
3. Cut `release/<version>` from `develop` when preparing a release.
4. Merge the release branch into `main` and tag it.
5. Merge the release branch back into `develop`.
6. For urgent production fixes, branch `hotfix/<name>` from `main`, then merge it into both `main` and `develop`.

## Local Build

```sh
cmake --preset vcpkg -DCMAKE_PREFIX_PATH=/path/to/Qt/6.10/<platform>
cmake --build --preset vcpkg
```
