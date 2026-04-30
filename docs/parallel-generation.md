# Parallel DLAF Generation Design

## Goal

Speed up DLAF generation while preserving the current global DLA behavior as
the first production strategy. The first parallel algorithm should produce
visually and statistically similar structures, but it does not need to match a
serial run particle-for-particle.

The initial strategy is speculative parallel candidate generation with
deterministic serial validation and commit. More aggressive strategies such as
strand ownership, spatial partitioning, and multi-seed merging are deferred
until the baseline is implemented and measured.

## Current Serial Constraints

Generation currently has a strict append-only dependency:

1. Start from a single root seed at the origin.
2. Start a walker from the current model bounding radius.
3. Random-walk until it reaches the attraction distance of the nearest existing
   particle.
4. Apply adhesion rules.
5. Place the final particle relative to that parent.
6. Insert the particle into the live spatial index.

Every new particle can observe all previously committed particles. The current
implementation also mutates per-parent join-attempt counters in `ShouldJoin()`,
so failed adhesion attempts affect future particles when non-default adhesion
settings are used.

The model is append-only. Particle indices are stable for the lifetime of a
generation run, so parent indices recorded by candidates or snapshots remain
valid even as newer particles are appended.

## Accepted Semantic Changes

Parallel mode may change the exact particle sequence relative to serial mode.
The contract is that the generated structure remains a valid DLAF-style result
under the configured parallel algorithm.

Reproducibility is required for a fixed full configuration:

- seed
- resolved thread count
- candidate batch size
- snapshot window size
- validation mode
- other generation parameters

The same seed does not need to produce identical output across different
parallel configurations or between serial and parallel modes.

## Public Data Model Changes

This design accepts source-breaking API changes in order to make the library's
data model clearer. That is separate from file compatibility: existing legacy
`.dlaf` assets must remain importable.

The current flat `DLAFScene::points` representation should be replaced with a
typed public point type:

```cpp
struct DLAFPoint
{
  float x;
  float y;
  float z;
};
```

Use `std::vector<DLAFPoint>` everywhere for scene points. The public point type
should be owned by DLAF, plain, standard-layout, and independent of the
internal math library.

Topology should be recorded as optional parent indices:

```cpp
std::vector<int32_t> parentIndices;
```

When topology is present, `parentIndices.size() == points.size()`. The root
particle uses parent index `-1`, and every non-root parent points to an earlier
particle. Legacy imports may leave topology empty.

Keep `distances`, `maxDistance`, and `radius` stored in `DLAFScene` for now.
Document `radius` carefully: it is scene particle/render radius derived from
the attraction distance, not the evolving model bounding radius.

Bounds should eventually become typed as two `DLAFPoint`s, but that can be a
separate source-breaking cleanup after typed points land.

## Parameter Changes

Generation input parameters should be treated as immutable. The current mutable
`DLAFParams::boundingRadius` behavior should move into internal model state.

Rename:

- `boundingRadius` to `initialBoundingRadius`
- `particleSpacing` to `placementFactor`

`placementFactor` preserves the current behavior: it is the interpolation
factor used by `PlaceParticle(parent, attachmentPoint)`, not a physical
distance. With the current default value of `1`, the final particle is placed
at the walker attachment point.

Migration notes for existing callers:

- `DLAFScene::points` changes from flat `std::vector<float>` triples to
  `std::vector<DLAFPoint>`.
- `DLAFParams::particleSpacing` becomes `placementFactor`.
- `DLAFParams::boundingRadius` becomes `initialBoundingRadius`.

Parallel configuration should be opt-in:

- `numThreads = 1` keeps serial behavior.
- `numThreads = 0` means auto-detect hardware concurrency.
- `candidateBatchSize = 0` means auto; in parallel mode, default to
  `2 * resolvedNumThreads`.
- `snapshotWindowSize = 0` means auto default, not disabled. Start with a small
  default such as `64` or `128`.

Parallel mode initially supports only default adhesion:

- `stubbornness == 0`
- `stickiness == 1`

If a caller requests parallel mode with unsupported adhesion settings, the new
result/error API should report an unsupported configuration. The legacy
`compute(params)` API can fall back to serial when it has no way to report the
error.

## Result, Progress, And Cancellation

Keep the legacy API for compatibility:

```cpp
DLAFScene compute(
    const DLAFParams &params,
    float *amountDone = nullptr,
    bool *cancel = nullptr);
```

Add a new result/error API before parallel mode lands. The new API should be
able to return status, scene, resolved configuration, and generation stats.

Suggested shape:

```cpp
struct DLAFStats
{
  uint64_t committedParticles;
  uint64_t generatedCandidates;
  uint64_t rejectedCandidates;
  uint64_t staleSnapshotRejects;
  uint64_t parentMismatchRejects;
  uint64_t attractionDistanceRejects;
  uint64_t snapshotRebuilds;
  uint64_t starvationRefreshes;
  uint64_t totalWalkSteps;
  uint64_t maxWalkSteps;
  double snapshotBuildSeconds;
};

struct DLAFResult
{
  DLAFScene scene;
  DLAFStats stats;
  /* status/error fields */
};
```

Internally, use a safer progress and cancellation context instead of sharing
raw non-atomic `float` and `bool` state across threads. Progress should be based
on committed particle count and requested particle count. The legacy wrapper can
still write normalized progress for existing callers.

On cancellation, stop accepting new committed particles immediately, let
running candidate tasks observe cancellation cooperatively, discard unfinished
candidates, join tasks cleanly, and return only fully committed particles.

## File Format

Introduce a versioned `.dlaf` binary format with a magic header and version.
New exports should write the versioned format by default and include topology
when available.

Keep the `.dlaf` extension and existing import/export function names. The
artifact is still the same domain object; versioning belongs inside the file.

Import must preserve legacy asset compatibility:

1. Read enough bytes to detect the new magic.
2. If the magic is absent, rewind and parse the existing legacy layout.
3. Validate expected byte counts so truncated or corrupt files fail clearly.

The file should store scene/topology only for now. Do not store generation seed,
parallel configuration, or stats until there is a clear consumer.

Even with `std::vector<DLAFPoint>`, the file payload can remain contiguous
float triples. Assert that `DLAFPoint` is standard-layout and exactly three
floats before writing point arrays directly.

Tool behavior:

- `split_dlaf` should initially drop topology and report that it was dropped
  when present.
- `upscale_dlaf` should initially drop topology because it replaces each input
  particle with displaced output particles.

## Execution Abstraction

Do not make the generation algorithm depend directly on raw `std::thread`
control flow. Introduce a small internal execution abstraction that can be
implemented with C++17 standard library facilities first and forwarded to
another tasking system later.

The first useful primitive is a `parallelFor(count, fn)`-style operation.
Public scheduler injection should wait until the interface is proven by the
internal implementation. For now, expose only generation parameters such as
thread count and candidate batch size.

## Spatial Indexing

Keep Boost `rtree` as the first spatial index backend, but hide it behind a
small internal `SpatialIndex` wrapper.

Workers must query immutable snapshot indexes. The coordinator is the only code
that owns and mutates the live model and live index. This avoids concurrent
mutation and leaves room to evaluate different nearest-neighbor structures
later.

Snapshots should contain only what workers need:

- typed points
- read-only spatial index
- immutable generation parameters
- snapshot size
- model bounding radius at snapshot construction time

Workers use the snapshot bounding radius consistently for starting positions
and resets. They do not read live model state.

## Candidate Model

Candidate sequence ids are monotonic attempt ids across the whole generation.
They are separate from final particle ids, which are assigned only on commit.

Each candidate should record enough data to validate and debug the serial join
decision:

- candidate sequence id
- snapshot size
- parent index
- original attachment point
- final placed point
- RNG stream id or seed
- walk step count

The RNG stream should be derived from the global seed and candidate sequence id,
not from worker id or scheduling order.

Rejected candidates never appear in the scene and do not count toward
`numParticles`.

## Parallel Algorithm

The first implementation should be round-synchronous and `parallelFor`-shaped:

1. Build an immutable snapshot from the committed model.
2. Assign the next `candidateBatchSize` sequence ids.
3. Use `parallelFor(candidateBatchSize, fn)` to generate one candidate per
   sequence id from the snapshot.
4. Wait for the batch to finish.
5. Validate candidates in deterministic sequence-id order against the live
   model.
6. Commit accepted candidates one at a time.
7. Reuse the same snapshot until `snapshotWindowSize` accepted commits have
   accumulated, then rebuild.
8. Repeat until the requested particle count is reached or cancellation occurs.

Accepted candidates from the same round are validated against the live model
after earlier candidates in that same round have already committed. This catches
collisions between same-snapshot candidates and preserves the serial commit
invariant.

The first parallel mode preserves the current one-seed-at-origin initialization.
Multi-seed generation is a separate future strategy.

The snapshot should usually be reused until its accepted-commit window expires.
Do not refresh snapshots based on rejection rate in the first implementation,
but collect rejection stats. Add an internal deterministic starvation escape
hatch, such as refreshing after several consecutive batches with no accepted
candidates. Keep that threshold internal at first and count refreshes in stats.

## Commit Validation

Validation replays the decisive serial join query, not the whole random walk.
For the first strict baseline, accept a candidate only when:

1. The candidate is not stale:
   `candidate.snapshotSize + snapshotWindowSize >= liveSize`.
2. The live nearest particle to the candidate's original attachment point is
   the same parent index recorded by the snapshot candidate.
3. The attachment point is still within `attractionDistance` of that parent.

Commit the candidate's already computed final placed point. Do not add an extra
clearance check against the final placed point in the first baseline, because
the serial algorithm does not perform that check.

Reparenting to a newer live nearest particle is deferred to a future
approximation mode.

## Non-Goals For First Implementation

- Exact support for non-default `stubbornness` and `stickiness` in parallel
  mode.
- Identical output across different thread counts or parallel configs.
- Strand ownership or branch-local growth.
- Multi-seed generation and cluster merging.
- Public scheduler/task-system ABI.
- Reparent-on-validation approximation.
- Extra final-position collision validation.
- Storing generation stats or configuration in `.dlaf` files.
- Topology-preserving `split_dlaf` or `upscale_dlaf`.

## Implementation Milestones

1. Add tests infrastructure around the existing serial generator and file I/O.
2. Introduce `DLAFPoint`, typed scene points, and source migration updates.
3. Add optional `parentIndices` and populate topology during serial generation.
4. Introduce versioned `.dlaf` export/import with legacy import support.
5. Rename/clarify parameters: `placementFactor` and `initialBoundingRadius`.
6. Add deterministic seeded serial RNG.
7. Add the new result/error API, progress context, cancellation context, and
   stats structure.
8. Add the internal execution abstraction with a C++17 default backend.
9. Wrap the spatial index and add immutable snapshot construction.
10. Implement candidate generation from snapshots.
11. Implement deterministic validation and serial commit of parallel batches.
12. Add parallel reproducibility tests for fixed configs.
13. Add benchmarks for `1k`, `10k`, and `100k` particles, reporting wall time,
    committed particles per second, rejection breakdown, walk steps, and
    snapshot rebuild cost. Benchmarks should compare seeded serial generation
    against seeded parallel generation for fixed configurations, but should not
    require identical geometry between serial and parallel runs.

## Future Strategies

After the baseline is implemented and measured, use parent topology and stats
to evaluate more aggressive algorithms:

- active frontier or strand ownership
- spatially sharded frontier growth
- larger candidate batches with streaming task groups
- validation modes that allow reparenting
- exact replay support for non-default adhesion
- alternative spatial indexes such as voxel grids, BVHs, or nanoflann
- topology-aware split and upscale tools
