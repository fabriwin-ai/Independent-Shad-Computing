# Dependencies and Header License Audit

Status: audit only. No third-party source or header has been copied into this repository.

The project itself is licensed under CC0-1.0. External dependencies keep their own licenses and notices.

## Current dependencies

| Dependency | Upstream | Current upstream ref checked | License | Intended role | Integration status |
|---|---|---|---|---|---|
| simple-ecs | https://github.com/lchsk/simple-ecs | master @ `1e887ecc28f36e41604fc21a4bf311a0e778892a` | MIT | ECS/data organization layer | Not integrated |
| enkiTS | https://github.com/dougbinks/enkiTS | master @ `404a3bf8f855039dfff2052184d6308655286c07` | Zlib | Parallel task/data scheduling | Not integrated |

## Headers inspected

### simple-ecs

Upstream describes the project as header-only.

Primary header:
- `include/simple_ecs.hpp`
- upstream blob: `ea582ba9a6f182ffca12f25eff027ecb78ce1341`

License:
- MIT
- copyright notice: Maciej Lechowski, 2018
- MIT requires the copyright and permission notice to remain with copies or substantial portions.

Decision:
- Suitable for the intended permissive project distribution.
- Do not remove or replace the upstream MIT notice when the header is eventually vendored or redistributed.

### enkiTS

Relevant public headers found in the current upstream tree:
- `src/TaskScheduler.h`
- `src/TaskScheduler_c.h`
- `src/LockLessMultiReadPipe.h`

Current upstream blob references:
- `TaskScheduler.h`: `9d1d962ff7f5506b65e259aea64fbe6572de01a0`
- `TaskScheduler_c.h`: `4a698bd07aa9c950712e2d4fcad547d5b91bf64a`
- `LockLessMultiReadPipe.h`: `339c8bdd486c1113f763cb2ef013aa5406f56f6f`

License:
- Zlib License
- copyright notice: Doug Binks, 2013
- the upstream license permits commercial use, modification and redistribution, subject to its notice, origin and modified-version conditions.

Decision:
- Suitable for the intended permissive project distribution.
- Preserve the upstream license notice.
- Clearly mark altered upstream source if any is modified.

## What is not selected yet

No graphics API has been selected for the first GPU backend.

Therefore no Vulkan, Direct3D, OpenGL, Metal, CUDA, or other graphics/compute SDK headers are being added or approved at this stage. Their licenses should be audited only after the backend API is selected.

## Integration policy

1. Keep third-party dependencies external until the project has a build system and reproducible dependency pinning.
2. Prefer pinned commits or immutable release versions over floating branch references.
3. Do not assume the project CC0-1.0 changes the license of a dependency.
4. Keep dependency-specific notices with any redistributed or vendored dependency.
5. Keep API-specific graphics headers behind backend interfaces.
6. Avoid introducing copyleft dependencies into the core runtime unless explicitly reviewed as a project-level licensing decision.

## Next dependency actions

- [ ] Establish CMake dependency mechanism.
- [ ] Pin simple-ecs to an explicit revision or release.
- [ ] Pin enkiTS to an explicit revision or release.
- [ ] Decide whether dependencies are fetched, vendored, or provided by the host build.
- [ ] Audit the selected graphics/compute SDK headers after the first backend is chosen.
- [ ] Generate a third-party notice bundle when external code is actually redistributed.
