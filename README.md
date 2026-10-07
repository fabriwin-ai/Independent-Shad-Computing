# Independent-Shad-Computing

An engine-independent graphics enhancement layer built around a small declarative mini-language.

The project sits above an existing game or graphics engine without modifying its internal rendering architecture.

## Core directives

The initial language exposes two technical primitives:

    !float
    !artifact

These directives compile into an intermediate representation and are executed through an independent runtime.

The system focuses on framebuffer processing, compute/render synchronization, parallel CPU scheduling, GPU compute passes, numerical parameter optimization, and dynamic level-of-detail execution.

Texture authoring, artistic presets, visual style definitions and material-generation logic are outside the core scope.

## Architecture

    Existing Game Engine
            |
            v
    Independent-Shad Layer
            |
       lexer / parser
            |
            v
          AST / IR
            |
            v
      execution planner
            |
       +----+----+
       |         |
    simple-ecs  enkiTS
       |         |
       +----+----+
            |
      task dependency graph
            |
       +----+----+----+
       |         |    |
     !float   gradient !artifact
       +---------+----+
                 |
          synchronization
                 |
            render/compute
                 |
                 v
              display

simple-ecs is used as an execution/data organization layer. enkiTS provides task and data parallel scheduling and dependency management.

## Mini-language

Example:

    !float {
        precision = fp32;
        accumulation = fp32;
    }

    !artifact {
        source = framebuffer;
        gradient = enabled;
        iterations = 4;
        weight = 0.25;
    }

    render {
        input = framebuffer;
        output = display;
    }

The language describes the enhancement pipeline. It does not replace GLSL, HLSL, C++, Vulkan, DirectX or another graphics API.

## !float

Defines numerical execution properties such as precision, accumulation and parameter representation.

## !artifact

Defines an executable enhancement stage. A stage may execute through a CPU task, GPU compute pass, render pass, or hybrid backend.

## Gradient optimization

Gradient descent is treated as parameter optimization rather than backpropagation through the entire host game engine.

    parameters
        |
        v
    forward pass
        |
        v
    loss evaluation
        |
        v
    gradient calculation
        |
        v
    parameter update
        |
        +---- repeat

## Parallel execution

Independent stages are represented as tasks and scheduled according to their dependencies.

    Capture
      |
      +-----------+
      |           |
      v           v
    Float     Artifact
      |           |
      +-----+-----+
            |
            v
        Gradient
            |
            v
         Composite
            |
            v
          Display

## LOD

Level of Detail controls execution cost rather than visual authoring.

- LOD 0: bypass
- LOD 1: single enhancement pass
- LOD 2: enhancement + gradient pass
- LOD 3: iterative optimization

See LOD.md.

## Non-goals

The project does not attempt to redesign a game engine, replace its renderer, modify simulation or physics, implement a complete shading language, or provide texture-authoring tooling.

## Initial build target

- C++
- CMake
- simple-ecs
- enkiTS

The first implementation should contain a CPU/reference backend before introducing GPU-specific complexity.

## Dependencies and licensing

The project itself is CC0-1.0. External dependencies retain their own licenses.

The current dependency audit identifies:

- simple-ecs: MIT, header-only, upstream revision `1e887ecc28f36e41604fc21a4bf311a0e778892a`
- enkiTS: Zlib, upstream revision `404a3bf8f855039dfff2052184d6308655286c07`

No third-party source or header is currently copied into this repository. See [DEPENDENCIES.md](DEPENDENCIES.md) for the header-level audit and integration policy.

## Design rule

The host engine remains the host.

Independent-Shad-Computing owns only its enhancement pipeline, scheduling, numerical processing, synchronization abstraction and output handoff.
