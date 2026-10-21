# Shader-cache startup

These are unreleased changes on `main`. The v0.2.0 download is unchanged.

## Behavior and diagnostics

The launcher still finishes cached-pipeline precompilation before gameplay. It
explicitly enables the existing parallel compiler pool (CPU threads minus two,
at most six workers). Set `GOW3_PARALLEL_WARMUP=0` before opening the launcher for
a serial comparison. Background preloading remains disabled.

Loose shader records are inventoried once during preload. Startup logs include
scan/read time, deserialization and preparation time, shader-module time,
pipeline construction CPU time, overall time, records/bytes, worker count,
process peak working set and a fingerprint of accepted pipeline keys. Parallel
CPU times are summed across workers and need not add up to wall time. Process
peak memory includes the entire process, not just cache memory.

The loading screen distinguishes reading, one-time optimization, preparing
shaders, building pipelines and finalizing the cache. Skipped and failed entries
are reported separately. Bad/truncated disk records fail individually rather
than terminating the preload deserializer.

## Packed storage gate

Packed storage is **opt-in** until full-startup and gameplay validation pass:

```powershell
$env:KZSF_PACKED_SHADER_CACHE='1'
python launcher/kzsf_local.py
```

The first compatible-cache preload converts `.key`, `.meta` and `.spv` files to
`user/cache/CUSA00190/packed-v1/`. `profile.bin` and the driver-specific
`pipeline-binaries/` directory retain their existing formats and compatibility
checks. Packing does not reduce the number of driver binary files.

The storage format is little endian, version 1. `data` contains uncompressed
records. `index` has a checksummed magic/version/count/sequence header followed
by named records containing 64-bit offset, length, content checksum and sequence
number, plus an index-record checksum. Names retain their type extension.
`journal` records new append-only updates until an atomic index snapshot commits
them. Bounds, names, index sizes and individual blob allocations are limited.
Each reader thread has its own persistent stream and file position.

Only the lease-owning process writes the cache. The bounded writer queue drains
accepted writes before close. Index snapshots are flushed and atomically
replaced before the journal is cleared; incomplete journal tails are ignored.
Corrupt data records are rejected individually and can be rebuilt. An unreadable
index falls back to loose files; if no loose backup remains, those records must
be rebuilt. Shader serialization/profile versions are independent of this
storage version.

Migration writes to `packed-v1.migrating`, checks free space (payload plus 256 MiB
headroom), verifies every copied record, then atomically activates it. Existing
loose records remain until a completed preload without read/build failures.
Cleanup deletes only copies whose content matches a packed record. Interrupted
conversion or insufficient space leaves the loose reader available.

For isolated comparisons, `KZSF_CACHE_KEEP_LOOSE=1` retains rollback copies.
`KZSF_PACKED_SHADER_CACHE=0` selects the legacy reader; after normal migration
cleanup it cannot read packed records, so keep packing enabled to reuse them.
Do not remove the packed store unless rebuilding its contents is intended.

## Validation

Build the game and explicit test/benchmark targets with MSYS2 CLANG64:

```bash
ninja -C out/gpu gow3-probe shader-cache-store-test warmup-inbox-test benchmark-shader-cache
./out/gpu/shader-cache-store-test.exe
./out/gpu/warmup-inbox-test.exe
./out/gpu/sampler-resources-test.exe
```

Storage tests cover migration, cancellation, disk-space fallback, packed and
loose reads, duplicate updates, independent concurrent readers, corrupt payloads,
invalid offsets, truncated indexes, crash journal replay, incompatible-cache
clear, and bounded writer drain/close behavior. The sampler serialization test
also checks recoverable truncation and invalid length rejection.

The storage-only benchmark makes a separate copy under `out/`, rotates run order
and verifies record counts, total bytes and a content fingerprint:

```powershell
python tools/benchmark-shader-cache.py --source 'C:/Port/user/cache/CUSA00190' --work out/cache-io-comparison
```

This excludes GPU-driver binaries and pipeline creation. It does **not** establish
the full-startup performance acceptance target.

The complete-startup runner snapshots shader files and hard-links driver binary
payloads into a private user directory. Supported builds atomically replace
binary files; they never modify linked payloads in place. Use a baseline built
from v0.2.0 and the current candidate, on the same volume as the source cache:

```powershell
python tools/benchmark-shader-startup.py --source 'C:/Port/user/cache/CUSA00190' --work out/cache-startup-comparison --baseline 'C:/Baseline/out/gow3-probe-launcher.exe'
```

Close the running game first. This compares three repeat runs of the baseline,
current loose reader and packed reader, excluding the first migration run.
Baseline processes are stopped after their complete-preload marker. Candidate
processes use a diagnostic exit after draining writes, before gameplay/saves.
This is not a substitute for manual gameplay and normal launch/quit checks.

For reboot-cold measurements, first prepare/migrate the snapshot using repeat
runs, reboot, then select exactly one measurement; repeat after another reboot
for the other modes:

```powershell
python tools/benchmark-shader-startup.py --work out/cache-startup-comparison --baseline 'C:/Baseline/out/gow3-probe-launcher.exe' --only baseline --reboot-cold
```

Use `--only loose` and `--only packed` after separate reboots. The tool never
reboots the machine or stops an existing game. Keep reboot-cold results separate
from OS-cache-warm results.

Enable packed storage by default only after at least 20% lower median repeat
startup time on the large cache, matching accepted pipeline counts and candidate
key fingerprints, bounded memory, and visual/chapter/normal-exit checks. If that
target is missed, retain the opt-in gate and use stage timings to decide the next
optimization. Do not publish a replacement v0.2.0 asset.

## Development measurements

On the development Ryzen 5 7500F / Radeon RX 9070 XT system, using an isolated
623,482-entry snapshot and three rotated repeat runs per mode:

| Mode | Median process start to completed preload |
|---|---:|
| v0.2.0 baseline | 11.438 s |
| Current loose storage, parallel compilation | 10.828 s |
| Current packed storage, parallel compilation | 2.516 s |

Packed repeat startup was 78.0% faster in this OS-cache-warm test. Every run
accepted 9,956 of the same 34,868 recorded pipeline entries. Both current storage
modes produced the same accepted-key fingerprint (`97e6ef3666452656`), with zero
failed builds or corrupt packed reads. Existing incompatible/stale entries were
skipped by the renderer's normal checks. Observed packed peak process working
set was approximately 2 GiB; this includes the native runtime and driver.

The separate storage test read 57,273 records / 325,741,750 bytes with matching
content fingerprints. Buffered, verified migration of that copy took 8.124 s
including its final read pass; it is excluded from repeat-startup medians.
The driver binary folder was neither repacked nor reformatted.

These measurements do not establish reboot-cold performance or visual/gameplay
equivalence. Packed storage remains opt-in pending those checks. Detailed local
logs and reports are under ignored `out/shader-full-startup-benchmark/` and
`out/shader-cache-storage-benchmark/`; no captures or cache payloads belong in Git.
