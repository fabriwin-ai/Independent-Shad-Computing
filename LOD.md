# LOD - Level of Detail

LOD controls computational complexity, not texture or artistic detail.

## LOD 0 - Bypass

No enhancement processing.

    framebuffer -> display

Use when the system is outside its frame-time budget or when enhancement is disabled.

## LOD 1 - Basic

Single enhancement pass.

    framebuffer
        |
      artifact
        |
      display

No iterative gradient optimization.

## LOD 2 - Extended

Enhancement plus gradient processing.

    framebuffer
        |
      artifact
        |
      gradient
        |
     composite
        |
      display

Suitable when additional compute time is available.

## LOD 3 - Iterative

Full optimization pipeline.

    framebuffer
        |
      forward
        |
       loss
        |
     gradient
        |
  parameter update
        |
      repeat
        |
     composite
        |
      display

Iteration count should be constrained by the frame budget.

## Adaptive selection

The runtime should monitor:

- CPU frame time
- GPU frame time
- queue latency
- synchronization latency
- memory pressure
- previous-frame execution time

A simple policy is:

    if frame_time > budget:
        reduce LOD

    if frame_time < budget * threshold:
        allow higher LOD

Use hysteresis to prevent oscillation between levels.

## Future LOD dimensions

LOD may eventually become multidimensional:

- task count
- gradient iterations
- numerical precision
- resolution scale
- compute frequency
- update interval

These dimensions remain independent from texture authoring and visual style.
