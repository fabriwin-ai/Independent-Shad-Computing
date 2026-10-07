# TODO

Legend:

- `[x]` done - compiled and covered by a passing test or a verified run
- `[~]` written but not compiled / not tested, or partially done (see note)
- `[ ]` not started

Status snapshot: 2026-10-06, first successful build.

- Toolchain: MSVC 19.51 (VS 18 2026), Windows SDK 10.0.26100, CMake 4.4.3, Vulkan SDK 1.4.363.
- Preset `msvc-offline` (enkiTS and simple-ecs OFF - GitHub was unreachable from the dev machine).
- Zero errors and zero `/W4` warnings across `isc_core`, `isc_tests` and `isc_run`.
- `ctest`: 11/11 areas pass; `isc_tests`: 78 pass, 0 fail, 1 skip (enkiTS not built).
- Vulkan verified on Intel HD Graphics Gen11 (driver Vulkan 1.3.215): GPU output matches the
  CPU reference (max abs diff 0.0 at 1280x720), zero validation-layer messages.

Anything that depends on enkiTS or simple-ecs is still `[~]`: it is wired in CMake but has never
been fetched or compiled. Build the `msvc` preset on a machine with GitHub access to close those.

## Phase 0 - Skeleton

- [x] Establish CMake project. (`CMakeLists.txt`, `CMakePresets.json`, `cmake/`)
- [x] Create include/, src/, shaders/, examples/, tests/.
- [x] Add README.md.
- [x] Add LOD.md.
- [~] Define dependency management. (FetchContent in `cmake/IscDependencies.cmake`; offline path verified, fetch path never run)
- [x] Audit current dependency and header licenses.
- [x] Pin dependency revisions or releases. (enkiTS `404a3bf`, simple-ecs `1e887ec`)
- [x] Decide fetched vs vendored dependency policy. (fetched + pinned; offline via `FETCHCONTENT_SOURCE_DIR_<NAME>` or `ISC_WITH_*=OFF`)

## Phase 1 - Core data model

- [x] Define Frame. (`include/isc/core/frame.hpp`)
- [x] Define RenderTarget. (`include/isc/core/render_target.hpp`, RGBA32F)
- [x] Define ArtifactPass. (`ir::Artifact`)
- [x] Define FloatConfig. (`ir::Numerics`)
- [x] Define GradientConfig. (folded into `ir::Artifact`: gradient, iterations, learning_rate, target, weight bounds)
- [ ] Define ExecutionContext.
- [~] Define ResourceHandle. (generational handle in `core/types.hpp`; not used by any pool yet)
- [x] Define frame lifecycle. (`FrameStage`, enforced by `Runtime::process`)
- [~] Define CPU/GPU ownership states. (`Ownership` enum set by the backends; transitions not validated)

## Phase 2 - Mini-language

- [x] Define lexical tokens.
- [x] Define identifiers and literals. (identifiers, integers, floats incl. exponents, strings)
- [x] Define blocks and comments. (`//` and `/* */`, UTF-8 BOM skipped)
- [x] Implement !float.
- [x] Implement !artifact.
- [x] Implement render.
- [x] Implement syntax errors. (with recovery at `;` and `}`)
- [x] Implement source locations. (line, column, offset)
- [x] Add parser tests. (`tests/test_lexer.cpp`, `tests/test_parser.cpp`)

Grammar (extended - optional block names and the `profile` block are additions):

    program     := declaration*
    declaration := header [identifier] "{" field* "}"
    header      := "!float" | "!artifact" | "render" | "profile"
    field       := identifier "=" value ";"
    value       := identifier | integer | float | string

## Phase 3 - IR

- [x] Define IR node types. (`include/isc/ir/ir.hpp`)
- [x] Define float nodes.
- [x] Define artifact nodes.
- [x] Define resource references. (`framebuffer` or artifact name)
- [x] Define task dependencies. (artifact source graph + topological order; per-frame task graph in the CPU backend)
- [~] Define execution priority. (`TaskPriority` exists and is honoured by the scheduler; not assigned from IR)
- [ ] Define synchronization points.
- [x] Implement AST -> IR.
- [x] Validate IR. (unknown/duplicate keys, types, ranges, unknown refs, cycles, dead artifacts)

## Phase 4 - ECS

- [~] Integrate simple-ecs. (FetchContent + interface target; never fetched/compiled, no code uses it)
- [ ] Define pipeline entities.
- [ ] Define components.
- [ ] Define execution systems.
- [ ] Keep host-engine ECS independent.

## Phase 5 - enkiTS scheduler

- [~] Integrate enkiTS. (FetchContent wiring; never fetched/compiled - GitHub unreachable)
- [x] Implement scheduler wrapper. (`sched::Scheduler`; serial backend tested, enkiTS backend `[~]`)
- [x] Implement task abstraction. (`sched::TaskGraph`)
- [x] Implement dependency graph. (TaskGraph levels/topological order tested; `enki::Dependency` mapping `[~]`)
- [~] Implement parallel artifact tasks. (float-stats || artifact-detail are independent graph nodes; only run serially so far)
- [~] Implement parallel numerical tasks. (`parallel_for` in the CPU kernels; serial scheduler only)
- [x] Implement priorities. (serial tie-breaking tested; mapping to `enki::TaskPriority` `[~]`)
- [ ] Implement frame synchronization.
- [~] Implement clean shutdown. (`WaitforAllAndShutdown` in the enkiTS destructor; untested)

## Phase 6 - CPU reference backend

- [x] Implement framebuffer input/output. (`RenderTarget` in/out, PPM read/write in `util/image_io`)
- [x] Implement !float. (fp32/fp64 precision and accumulation templates)
- [x] Implement !artifact.
- [x] Implement CPU artifact pass. (blur + detail extraction)
- [x] Implement CPU gradient pass. (edge-energy reduction)
- [x] Implement parameter optimization.
- [x] Add deterministic reference output. (identical across schedulers and runs)
- [x] Add numerical tests. (`tests/test_cpu_backend.cpp`)

## Phase 7 - Gradient descent

- [x] Define parameter container. (`opt::ParameterSet`)
- [x] Define loss interface. (`opt::EdgeEnergyLoss`)
- [x] Define gradient interface.
- [x] Implement finite-difference fallback.
- [x] Implement analytic gradients where applicable. (matches finite differences)
- [x] Implement learning rate.
- [x] Implement iteration limits.
- [x] Implement convergence criteria.
- [x] Implement parameter bounds.
- [x] Add stability tests. (flat images, zero iterations)

## Phase 8 - Render/compute abstraction

- [x] Define backend interface. (`isc::Backend`, CPU + Vulkan implementations)
- [ ] Define compute pass interface. (passes are internal to each backend)
- [ ] Define render pass interface.
- [ ] Define resource transition interface. (only the `Ownership` enum so far)
- [ ] Define submission abstraction.
- [ ] Define GPU completion abstraction.
- [x] Keep API-specific synchronization behind backend interfaces. (`vulkan.h` never appears in `include/`)

## Phase 9 - First GPU backend

- [x] Select first graphics API. (Vulkan - Vulkan SDK 1.4.363 on the dev machine)
- [x] Implement device initialization. (`vk_context`: headless instance/device, queue + caps query)
- [x] Implement framebuffer import. (Upload buffer, zero-copy on UMA)
- [x] Implement compute resources. (`vk_allocator` block sub-allocator + LRU buffer cache)
- [x] Implement artifact compute pass. (`blur_h.comp`, `detail.comp`, `resample.comp`)
- [x] Implement gradient compute pass. (`reduce.comp`, `optimize.comp` - descent runs on the GPU)
- [x] Implement composite pass. (`composite.comp`)
- [x] Implement output transfer. (Readback buffer in HOST_CACHED memory)
- [x] Implement GPU synchronization. (barriers + one fence per frame; synchronous hand-off)
- [x] Validate against CPU reference. (`tests/test_vulkan.cpp`, `isc_run --compare`)

## Phase 10 - Frame pipeline

- [x] Implement frame capture.
- [x] Build task graph per frame. (CPU backend)
- [~] Execute independent stages concurrently. (graph allows it; needs the enkiTS build)
- [x] Resolve dependencies.
- [x] Submit compute work.
- [x] Synchronize completion.
- [x] Composite result.
- [x] Present output. (hand-off back to the host's render target; the host owns presentation)
- [ ] Measure CPU/GPU overlap.

## Phase 11 - LOD

- [x] Implement LOD 0.
- [x] Implement LOD 1.
- [x] Implement LOD 2.
- [x] Implement LOD 3.
- [x] Define frame-time budget. (`RenderProfile::target_fps`, `enhance_budget_ms`)
- [ ] Define CPU budget.
- [ ] Define GPU budget. (GPU time is measured via timestamps but not fed to the controller)
- [x] Implement adaptive LOD.
- [x] Implement hysteresis. (plus dead band; rung pinning for benchmarks)
- [x] Add performance telemetry. (`FrameStats`: layer/GPU ms, load, memory, per-stage weights and losses)

## Phase 11b - Render-enhance profiles (new)

- [x] Profile settings struct. (`include/isc/core/profile.hpp`)
- [x] Built-in profiles: performance / balanced / quality.
- [x] `profile <name> { ... }` block with `base =` inheritance and validation.
- [x] Dynamic resolution settings. (scale min/max/step/start)
- [x] Memory allocation settings. (memory_budget_mb, memory_block_mb)
- [x] Buffer cache size setting + byte-budgeted LRU cache. (`core/resource_cache.hpp`)
- [x] Device auto-tuning. (`tune_profile_for_device`: UMA heap share, maxMemoryAllocationSize, queue topology)
- [x] Intel HD Graphics Gen11 device preset. (from the gpuinfo JSON profile; `examples/intel_gen11.fa`)
- [x] Dynamic resolution controller (quality ladder LOD x scale).
- [x] Memory-aware resolution cap.
- [x] Vulkan device-memory block allocator honouring the budget.
- [x] Profile tests. (`tests/test_profile.cpp`)

## Phase 12 - Host integration

- [~] Define minimal host API. (`isc::Runtime::load` / `process`; no engine-facing handles yet)
- [~] Define framebuffer import. (CPU-side `RenderTarget` only; no native texture import)
- [~] Define framebuffer export.
- [ ] Define frame begin/end.
- [ ] Define resource lifetime.
- [ ] Define synchronization ownership.
- [ ] Implement one host adapter.
- [ ] Verify host-engine source remains untouched.

## Phase 13 - Testing

- [x] Parser tests.
- [x] IR validation tests.
- [ ] ECS tests.
- [x] Scheduler tests. (serial; the enkiTS test skips in the offline build)
- [x] Dependency tests.
- [x] Numerical tests.
- [x] Gradient tests.
- [x] CPU/GPU equivalence tests.
- [ ] Synchronization tests.
- [x] LOD tests.
- [x] Resource lifetime tests. (cache eviction/clear/destructor, continuous-frame buffer reuse)

## Phase 14 - Profiling

- [ ] Measure parser overhead.
- [ ] Measure IR compilation.
- [ ] Measure scheduler overhead.
- [ ] Measure synchronization cost.
- [ ] Measure CPU utilization.
- [ ] Measure GPU utilization.
- [ ] Measure memory bandwidth.
- [~] Measure frame latency. (per-frame layer/GPU ms in `isc_run`; no latency breakdown yet)
- [ ] Measure LOD transitions.

## Phase 15 - Hardening

- [~] Handle invalid mini-language input. (diagnostics with recovery; fuzzing pending)
- [ ] Handle missing resources.
- [~] Handle GPU allocation failure. (budget error + cache flush/retry path; untested)
- [~] Handle synchronization failure. (fence timeout -> error + `vkDeviceWaitIdle`; untested)
- [~] Handle shader compilation failure. (SPIR-V built and validated at build time; pipeline errors reported)
- [ ] Handle device loss.
- [ ] Handle scheduler shutdown.
- [x] Validate external configuration. (profile validation)
- [~] Remove unnecessary frame allocations. (steady-state Vulkan frames allocate nothing; CPU path reuses targets)

## Phase 16 - Optimization

- [ ] Cache compiled IR.
- [~] Cache pipeline state. (`VkPipelineCache` created; not persisted to disk)
- [ ] Reuse task objects.
- [x] Reuse GPU resources. (LRU buffer cache wired into the Vulkan backend; hits verified)
- [~] Reduce frame allocations.
- [ ] Batch compatible operations.
- [ ] Merge compatible compute passes where safe.
- [ ] Avoid unnecessary CPU/GPU synchronization. (currently one blocking fence wait per frame)
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
- [x] Add minimal examples. (`examples/*.fa`, `examples/isc_run`)

## Definition of done

- [x] .fa source containing !float and !artifact parses.
- [x] Source compiles into IR.
- [x] IR generates an executable task graph.
- [ ] enkiTS executes independent stages in parallel. (needs the online `msvc` preset)
- [x] CPU backend produces deterministic output.
- [x] Gradient optimization operates on declared parameters.
- [x] LOD can reduce processing cost.
- [~] Render target can enter and leave the layer without modifying the host engine. (CPU-side hand-off only)
- [x] GPU execution matches the CPU reference within defined tolerance.
- [x] Pipeline executes continuously frame after frame.
