# TODO

Legend:

- `[x]` done
- `[~]` written, not yet compiled / tested (or partially done - see note)
- `[ ]` not started

Status snapshot: 2026-10-06. Nothing has been compiled yet in this snapshot;
items flip from `[~]` to `[x]` once `cmake --build` and `ctest` pass.

## Phase 0 - Skeleton

- [~] Establish CMake project. (`CMakeLists.txt`, `CMakePresets.json`, `cmake/`)
- [~] Create include/, src/, shaders/, examples/, tests/. (include/ and src/ exist; shaders/, examples/, tests/ pending)
- [x] Add README.md.
- [x] Add LOD.md.
- [~] Define dependency management. (FetchContent in `cmake/IscDependencies.cmake`)
- [x] Audit current dependency and header licenses.
- [~] Pin dependency revisions or releases. (pinned to the audited commit hashes)
- [~] Decide fetched vs vendored dependency policy. (fetched + pinned; offline via `FETCHCONTENT_SOURCE_DIR_<NAME>` or `ISC_WITH_*=OFF`)

## Phase 1 - Core data model

- [~] Define Frame. (`include/isc/core/frame.hpp`)
- [~] Define RenderTarget. (`include/isc/core/render_target.hpp`, RGBA32F)
- [~] Define ArtifactPass. (`ir::Artifact`)
- [~] Define FloatConfig. (`ir::Numerics`)
- [~] Define GradientConfig. (folded into `ir::Artifact`: gradient, iterations, learning_rate, target, weight bounds)
- [ ] Define ExecutionContext.
- [~] Define ResourceHandle. (generational handle in `core/types.hpp`)
- [~] Define frame lifecycle. (`FrameStage` enum; runtime enforcement pending)
- [~] Define CPU/GPU ownership states. (`Ownership` enum; transitions not enforced yet)

## Phase 2 - Mini-language

- [~] Define lexical tokens.
- [~] Define identifiers and literals. (identifiers, integers, floats incl. exponents, strings)
- [~] Define blocks and comments. (`//` and `/* */`)
- [~] Implement !float.
- [~] Implement !artifact.
- [~] Implement render.
- [~] Implement syntax errors. (with recovery at `;` and `}`)
- [~] Implement source locations. (line, column, offset)
- [ ] Add parser tests.

Grammar (extended - optional block names and the `profile` block are additions):

    program     := declaration*
    declaration := header [identifier] "{" field* "}"
    header      := "!float" | "!artifact" | "render" | "profile"
    field       := identifier "=" value ";"
    value       := identifier | integer | float | string

## Phase 3 - IR

- [~] Define IR node types. (`include/isc/ir/ir.hpp`)
- [~] Define float nodes.
- [~] Define artifact nodes.
- [~] Define resource references. (`framebuffer` or artifact name)
- [~] Define task dependencies. (artifact source graph + topological order; per-frame task graph pending)
- [~] Define execution priority. (`TaskPriority` exists; not assigned from IR yet)
- [ ] Define synchronization points.
- [~] Implement AST -> IR.
- [~] Validate IR. (unknown/duplicate keys, types, ranges, unknown refs, cycles, dead artifacts)

## Phase 4 - ECS

- [~] Integrate simple-ecs. (fetched + linked as interface target; no code uses it yet)
- [ ] Define pipeline entities.
- [ ] Define components.
- [ ] Define execution systems.
- [ ] Keep host-engine ECS independent.

## Phase 5 - enkiTS scheduler

- [~] Integrate enkiTS. (FetchContent wiring; could not be fetched/compiled on the dev machine - GitHub unreachable)
- [~] Implement scheduler wrapper. (`sched::Scheduler`, enkiTS + serial fallback)
- [~] Implement task abstraction. (`sched::TaskGraph`)
- [~] Implement dependency graph. (enki::Dependency per edge)
- [ ] Implement parallel artifact tasks.
- [ ] Implement parallel numerical tasks.
- [~] Implement priorities. (mapped to enki::TaskPriority)
- [ ] Implement frame synchronization.
- [~] Implement clean shutdown. (`WaitforAllAndShutdown` in destructor)

## Phase 6 - CPU reference backend

- [ ] Implement framebuffer input/output.
- [ ] Implement !float.
- [ ] Implement !artifact.
- [ ] Implement CPU artifact pass.
- [ ] Implement CPU gradient pass.
- [ ] Implement parameter optimization.
- [ ] Add deterministic reference output.
- [ ] Add numerical tests.

## Phase 7 - Gradient descent

- [ ] Define parameter container.
- [ ] Define loss interface.
- [ ] Define gradient interface.
- [ ] Implement finite-difference fallback.
- [ ] Implement analytic gradients where applicable.
- [ ] Implement learning rate.
- [ ] Implement iteration limits.
- [ ] Implement convergence criteria.
- [ ] Implement parameter bounds.
- [ ] Add stability tests.

## Phase 8 - Render/compute abstraction

- [ ] Define backend interface.
- [ ] Define compute pass interface.
- [ ] Define render pass interface.
- [ ] Define resource transition interface.
- [ ] Define submission abstraction.
- [ ] Define GPU completion abstraction.
- [ ] Keep API-specific synchronization behind backend interfaces.

## Phase 9 - First GPU backend

- [x] Select first graphics API. (Vulkan - Vulkan SDK 1.4.363 on the dev machine)
- [ ] Implement device initialization.
- [ ] Implement framebuffer import.
- [ ] Implement compute resources.
- [ ] Implement artifact compute pass.
- [ ] Implement gradient compute pass.
- [ ] Implement composite pass.
- [ ] Implement output transfer.
- [ ] Implement GPU synchronization.
- [ ] Validate against CPU reference.

## Phase 10 - Frame pipeline

- [ ] Implement frame capture.
- [ ] Build task graph per frame.
- [ ] Execute independent stages concurrently.
- [ ] Resolve dependencies.
- [ ] Submit compute work.
- [ ] Synchronize completion.
- [ ] Composite result.
- [ ] Present output.
- [ ] Measure CPU/GPU overlap.

## Phase 11 - LOD

- [ ] Implement LOD 0.
- [ ] Implement LOD 1.
- [ ] Implement LOD 2.
- [ ] Implement LOD 3.
- [~] Define frame-time budget. (`RenderProfile::target_fps`, `enhance_budget_ms`)
- [ ] Define CPU budget.
- [ ] Define GPU budget.
- [ ] Implement adaptive LOD.
- [ ] Implement hysteresis.
- [ ] Add performance telemetry.

## Phase 11b - Render-enhance profiles (new)

- [~] Profile settings struct. (`include/isc/core/profile.hpp`)
- [~] Built-in profiles: performance / balanced / quality.
- [~] `profile <name> { ... }` block with `base =` inheritance and validation.
- [~] Dynamic resolution settings. (scale min/max/step/start)
- [~] Memory allocation settings. (memory_budget_mb, memory_block_mb)
- [~] Buffer cache size setting + byte-budgeted LRU cache. (`core/resource_cache.hpp`)
- [~] Device auto-tuning. (`tune_profile_for_device`: UMA heap share, maxMemoryAllocationSize, queue topology)
- [~] Intel HD Graphics Gen11 device preset. (from the gpuinfo JSON profile)
- [ ] Dynamic resolution controller (quality ladder LOD x scale).
- [ ] Memory-aware resolution cap.
- [ ] Vulkan device-memory block allocator honouring the budget.
- [ ] Profile tests.

## Phase 12 - Host integration

- [ ] Define minimal host API.
- [ ] Define framebuffer import.
- [ ] Define framebuffer export.
- [ ] Define frame begin/end.
- [ ] Define resource lifetime.
- [ ] Define synchronization ownership.
- [ ] Implement one host adapter.
- [ ] Verify host-engine source remains untouched.

## Phase 13 - Testing

- [ ] Parser tests.
- [ ] IR validation tests.
- [ ] ECS tests.
- [ ] Scheduler tests.
- [ ] Dependency tests.
- [ ] Numerical tests.
- [ ] Gradient tests.
- [ ] CPU/GPU equivalence tests.
- [ ] Synchronization tests.
- [ ] LOD tests.
- [ ] Resource lifetime tests.

## Phase 14 - Profiling

- [ ] Measure parser overhead.
- [ ] Measure IR compilation.
- [ ] Measure scheduler overhead.
- [ ] Measure synchronization cost.
- [ ] Measure CPU utilization.
- [ ] Measure GPU utilization.
- [ ] Measure memory bandwidth.
- [ ] Measure frame latency.
- [ ] Measure LOD transitions.

## Phase 15 - Hardening

- [~] Handle invalid mini-language input. (diagnostics with recovery; fuzzing pending)
- [ ] Handle missing resources.
- [ ] Handle GPU allocation failure.
- [ ] Handle synchronization failure.
- [ ] Handle shader compilation failure.
- [ ] Handle device loss.
- [ ] Handle scheduler shutdown.
- [~] Validate external configuration. (profile validation)
- [ ] Remove unnecessary frame allocations.

## Phase 16 - Optimization

- [ ] Cache compiled IR.
- [ ] Cache pipeline state.
- [ ] Reuse task objects.
- [~] Reuse GPU resources. (generic resource cache written; not wired to a backend yet)
- [ ] Reduce frame allocations.
- [ ] Batch compatible operations.
- [ ] Merge compatible compute passes where safe.
- [ ] Avoid unnecessary CPU/GPU synchronization.
- [ ] Tune task granularity.
- [ ] Benchmark low-core-count systems.

## Phase 17 - Documentation

- [ ] Document mini-language.
- [ ] Document IR.
- [ ] Document execution model.
- [ ] Document scheduler.
- [ ] Document synchronization.
- [ ] Document gradient optimization.
- [ ] Document LOD.
- [ ] Document backend API.
- [ ] Add architecture diagrams.
- [ ] Add minimal examples.

## Definition of done

- [ ] .fa source containing !float and !artifact parses.
- [ ] Source compiles into IR.
- [ ] IR generates an executable task graph.
- [ ] enkiTS executes independent stages in parallel.
- [ ] CPU backend produces deterministic output.
- [ ] Gradient optimization operates on declared parameters.
- [ ] LOD can reduce processing cost.
- [ ] Render target can enter and leave the layer without modifying the host engine.
- [ ] GPU execution matches the CPU reference within defined tolerance.
- [ ] Pipeline executes continuously frame after frame.
