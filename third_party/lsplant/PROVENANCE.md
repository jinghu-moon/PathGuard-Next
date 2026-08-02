# LSPlant provenance

- Upstream: `https://github.com/LSPosed/LSPlant`
- Commit: `84256d4cb51abd79280da5c29437fb7004391667`
- License: LGPL-3.0 (`LICENSE`)
- User-supplied source: `refer/LSPlant-master`
- Verification: the supplied JNI source matched the upstream commit; only the declared
  DexBuilder submodule was absent locally.
- DexBuilder: `https://github.com/LSPosed/DexBuilder`, commit
  `ac7fb2230954ee311808bad469b0db501f31bfb8`, LGPL-3.0.
- parallel-hashmap: `https://github.com/greg7mdp/parallel-hashmap`, commit
  `0cd57d29a959256ed66b2afdd1009928fc625d09`, Apache-2.0.

PathGuard builds LSPlant into the separately replaceable
`libpathguard_lsplant.so`. The Zygisk module accesses it only through the C ABI
in `provider-adapter/native/include`.
