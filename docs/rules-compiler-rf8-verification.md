# RF8 rules compiler verification

Date: 2026-07-27

## Frozen stack

- Compiler: C++20, Release
- Parser: toml++ v3.4.0, TOML 1.0, exceptions/formatters/unreleased disabled
- Host: MSVC 19.44
- Fuzz: Clang/libFuzzer 20.1.7 with UBSan
- Android: Alioth/R1 device `dc39c31d`, arm64-v8a, API 31, NDK r27d

## Performance

CPU budgets exclude publish/fsync. Peak memory is process peak working set/RSS.

| Environment | Case | Source/generated | Rules | P95 | Peak |
|---|---|---:|---:|---:|---:|
| Host | no arrow | 60/60 B | 0 | 0.005 ms | 11.0 MiB |
| Host | typical | 7,519/11,103 B | 256 | 1.70 ms | 11.7 MiB |
| Host | large | 61,839/89,839 B | 2,000 | 14.63 ms | 14.6 MiB |
| Host | extreme | 128,911/186,255 B | 4,096 | 29.15 ms | 18.0 MiB |
| Alioth | no arrow | 60/60 B | 0 | 0.007 ms | 8.0 MiB |
| Alioth | typical | 7,519/11,103 B | 256 | 2.65 ms | 8.0 MiB |
| Alioth | large | 61,839/89,839 B | 2,000 | 17.69 ms | 8.0 MiB |
| Alioth | extreme | 128,911/186,255 B | 4,096 | 36.92 ms | 8.4 MiB |

Host publish including fsync: first 7.64 ms, replacement 3.60 ms.
Alioth publish including fsync: first 18.16 ms, replacement 7.21 ms.
The JSON Lines benchmark reports every required stage and enforces the frozen
10/50/100 ms and 16/24/32 MiB budgets.

## Fuzz and recovery

- Complete compile fixed run: 50,000 inputs, no crash or invariant failure.
- Complete compile timed run: 60 seconds, more than 2.42 million executions,
  coverage 5,699, feature coverage 18,351, peak RSS 44 MiB, no artifact.
- Daily CTest includes scanner, candidate, desugar and complete compile smoke.
- Publisher fault injection covers create, write, metadata, C++ reader/verifier,
  file fsync, rename and directory fsync.
- Reconciler tests cover invalid source, unsupported admission, publish failure,
  stale status, corrupt policy and restart recovery.

## Android vertical path

`pathguardd` compiled `rules.toml` and atomically published policy
generation `5668809052303656865`. The data-plane-only mmap/index probe found
`com.example.app` and built `Source -> Target`. A subsequent invalid
candidate failed compilation; policy SHA-256 stayed unchanged and the old plan
remained readable. Host/Android compiler parity and the final Zygisk ELF/link-map
isolation checks passed.
