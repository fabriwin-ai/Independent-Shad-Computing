# TODO

## Phase 0 - Skeleton

- [ ] Establish CMake project.
- [ ] Create include/, src/, shaders/, examples/, tests/.
- [ ] Add README.md.
- [ ] Add LOD.md.
- [ ] Define dependency management.
- [x] Audit current dependency and header licenses.
- [ ] Pin dependency revisions or releases.
- [ ] Decide fetched vs vendored dependency policy.

## Phase 1 - Core data model

- [ ] Define Frame.
- [ ] Define RenderTarget.
- [ ] Define ArtifactPass.
- [ ] Define FloatConfig.
- [ ] Define GradientConfig.
- [ ] Define ExecutionContext.
- [ ] Define ResourceHandle.
- [ ] Define frame lifecycle.
- [ ] Define CPU/GPU ownership states.

## Phase 2 - Mini-language

- [ ] Define lexical tokens.
- [ ] Define identifiers and literals.
- [ ] Define blocks and comments.
- [ ] Implement !float.
- [ ] Implement !artifact.
- [ ] Implement render.
- [ ] Implement syntax errors.
- [ ] Implement source locations.
- [ ] Add parser tests.

Initial grammar:

    program := declaration*
    declaration := float_block | artifact_block | render_block
    float_block := "!float" block
    artifact_block := "!artifact" block
    render_block := "render" block

## Phase 3 - IR

- [ ] Define IR node types.
- [ ] Define float nodes.
- [ ] Define artifact nodes.
- [ ] Define resource references.
- [ ] Define task dependencies.
- [ ] Define execution priority.
- [ ] Define synchronization points.
- [ ] Implement AST -> IR.
- [ ] Validate IR.

## Phase 4 - ECS

- [ ] Integrate simple-ecs.
- [ ] Define pipeline entities.
- [ ] Define components.
- [ ] Define execution systems.
- [ ] Keep host-engine ECS independent.

## Phase 5 - enkiTS scheduler

- [ ] Integrate enkiTS.
- [ ] Implement scheduler wrapper.
- [ ] Implement task abstraction.
- [ ] Implement dependency graph.
- [ ] Implement parallel artifact tasks.
- [ ] Implement parallel numerical tasks.
- [ ] Implement priorities.
- [ ] Implement frame synchronization.
- [ ] Implement clean shutdown.

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

- [ ] Select first graphics API.
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
- [ ] Define frame-time budget.
- [ ] Define CPU budget.
- [ ] Define GPU budget.
- [ ] Implement adaptive LOD.
- [ ] Implement hysteresis.
- [ ] Add performance telemetry.

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

- [ ] Handle invalid mini-language input.
- [ ] Handle missing resources.
- [ ] Handle GPU allocation failure.
- [ ] Handle synchronization failure.
- [ ] Handle shader compilation failure.
- [ ] Handle device loss.
- [ ] Handle scheduler shutdown.
- [ ] Validate external configuration.
- [ ] Remove unnecessary frame allocations.

## Phase 16 - Optimization

- [ ] Cache compiled IR.
- [ ] Cache pipeline state.
- [ ] Reuse task objects.
- [ ] Reuse GPU resources.
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
