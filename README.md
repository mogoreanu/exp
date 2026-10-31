# exp

Experimental C++ tools and libraries. Each directory is a Bazel package; they
share Abseil, GoogleTest, and Google Benchmark, but there is no single
application. Code lives in `namespace mogo` except `strings/`.

## Environment expectations

* Built with Bazel (Bzlmod, `MODULE.bazel`)
* Abseil for flags, logging, time, and status
* Aimed at VS Code on Linux
* Instructions verified on Debian 12

## Getting started

Install Bazel via Bazelisk (a newer Bazel than Debian's package is often
required). `.bazelversion` pins the Bazel release used by local builds and CI.

```bash
wget 'https://github.com/bazelbuild/bazelisk/releases/download/v1.29.0/bazelisk-linux-amd64'
chmod +x bazelisk-linux-amd64
sudo mv bazelisk-linux-amd64 /usr/bin/bazel
```

Install git and a C++ compiler compatible with Bazel:

```bash
sudo apt install git g++
```

Clone and run the smoke-test binary:

```bash
git clone https://github.com/mogoreanu/exp.git
cd exp
bazel run :my_hello
```

Optional: copy [buildifier](https://github.com/bazelbuild/buildtools/releases)
into `PATH` (for example `/usr/bin/buildifier`) to format `BUILD.bazel` files.

## Build and test

```bash
bazel build //...
bazel test //...
bazel test //... --test_tag_filters=-benchmark   # same filter as CI
bazel test //... --test_tag_filters=benchmark    # Google Benchmark targets only
```

`--` after `bazel run` passes flags through to the binary. `--stderrthreshold=0`
is the usual way to see Abseil `INFO` logs.

GitHub Actions (`.github/workflows/ci.yml`) runs `bazel build //...` and
`bazel test //... --test_tag_filters=-benchmark` on every push and pull request.

## Packages

### `:my_hello`

Abseil flags, logging, `Status`, and time sandbox. First binary to change when
learning the repo conventions.

```bash
bazel run :my_hello
bazel run :my_hello -- --stderrthreshold=0 --mytest=true
```

### `token_bucket/`

Duration-based rate limiters:

* `SimpleTokenBucket` — tokens are `absl::Duration`; no burst
* `RateTokenBucket` — wraps simple with a refill rate
* `BurstTokenBucket` — allows a configured burst
* `MultiTokenBucket` — smears a large take over future refill so one request
  does not head-of-line-block others

```bash
bazel test //token_bucket/...
bazel run token_bucket:token_bucket_demo -- --stderrthreshold=0
bazel run token_bucket:token_bucket_demo -- --stderrthreshold=0 --tb_type=burst
bazel run token_bucket:token_bucket_demo -- --stderrthreshold=0 --tb_type=multi
```

`--tb_type` also accepts `rate`. The demo reports request rate with
`stat:approx_counter`.

### `stat/`

Sliding-window counters:

* `approx_counter` — thread-compatible; cheap way to track request/byte rate
  over the last interval (default 1s)
* `throughput_counter` — lock-free; callers must invoke `CleanupSpans` or
  expired buckets wrap and inflate the rate
* `stat_utils` — power-of-two-friendly modulo used by `approx_counter`

```bash
bazel test //stat/...
```

### `block_validator/`

Long-running block-device (or file) contract tester. Issues biased small writes,
intentional overlaps, reads that check atomic write units, and TRIM. Validation
state is sharded per block; overlapping writes go through a slower history path.
The library is unit-tested against an in-memory mock device.

```bash
bazel test //block_validator/...
bazel run block_validator:block_validator -- \
  --target_file=/path/to/dev_or_file \
  --region_offset=0 --region_size=1G \
  --num_threads=4 --run_time_sec=60
```

`--target_file` is required. `--region_offset` and `--region_size` accept
human-readable sizes (`100M`, `1G`). The tool opens with `O_DIRECT | O_SYNC`.
`--validate_trim_zeroes` should match whether the device guarantees zeroes after
TRIM. See `block_validator/prompts.md` for the original requirements.

### `perf/`

Cycle-clock measurement:

* `tightloop` — histogram of loop (or sleep) latency in TSC cycles
* `time_histogram` — explicit/RAII spans
* `bits` — `Fls64` and log-bucket histograms (x86 `bsrq` helper in `Fls64Bsr`)
* `cycle_clock_utils` — TSC helpers

```bash
bazel test //perf/... --test_tag_filters=-benchmark
bazel run -c opt perf:tightloop -- --run_duration=10s --cycles_min=10 --cycles_shift=0
bazel test -c opt --test_output=streamed perf:time_histogram_benchmarks
```

For tighter numbers, pin the CPU and use the performance governor:

```bash
sudo apt-get install cpufrequtils
sudo cpufreq-set -r -g performance
bazel build -c opt --dynamic_mode=off perf:tightloop
taskset -c 0 bazel-bin/perf/tightloop --cycles_min=10 --cycles_shift=0 --run_duration=10s
```

Sleep wake-up consistency:

```bash
bazel run -c opt perf:tightloop -- --run_duration=10s \
  --duration_min=4ms --duration_shift=3us --sleep_duration=4000us
```

### `strings/`

`HumanReadableNumBytes` parses and formats sizes (`1G`, `100M`) for
`block_validator` flags. Binary units (1024). No tests yet.

### `test/`

Language experiments, currently tuple unwrapping:

```bash
bazel test --test_output=streamed test:tuple_unwrapping_test
```

## VS Code

Command Palette (`Ctrl+P`):

```
ext install ms-vscode.cpptools-extension-pack
ext install BazelBuild.vscode-bazel
ext install bpfdeploy.bpftrace
```

Workspace settings and IntelliSense live under `.vscode/` and `cfg/`.

Debug launch configs in `.vscode/launch.json`:

* **throughput_counter_test (GDB)** — builds `-c dbg` then launches GDB
* **multi_token_bucket_test (GDB)** — launches
  `bazel-bin/token_bucket/multi_token_bucket_test` (build that target first)
* **auto_detect (GDB)** — intended to map the open file to a Bazel target; the
  helper `.vscode/file_to_target_map.sh` currently only handles
  `stat/throughput_counter_*`

Use **Run and Debug** with one of those configs, or `bazel build -c dbg <target>`
and point GDB at `bazel-bin/...`.
