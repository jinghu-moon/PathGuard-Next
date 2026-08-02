# V-66 current-device Provider restart evidence

## Scope

- Device: Xiaomi M2012K11AC (alioth), Android 13/API 33, MIUI
  `V14.0.8.0.TKHCNXM`
- Installed module: `0.1.29-dev`
- Restart operation: force-stop both system Provider processes, issue public content queries,
  wait for new runtime status, then run the enhanced LSPlant collector.
- Evidence: `build/device-evidence/provider-lsplant-v1/20260802-100337`

## Results

| Provider | Before PID | After PID | Re-published status | Method group | errno |
| --- | ---: | ---: | --- | --- | ---: |
| ExternalStorageProvider | 12240 | 16784 | yes | 3/3/3/3 | 0 |
| MediaProvider | 4779 | 16667 | yes | 2044/2044/2044/2044 | 0 |

Both status records retained `build_matched=true`, `library_loaded=true`,
`lsplant_initialized=true`, `hooker_dex_loaded=true`, `action_total=2`, both admissions
`active`, and `observed_capabilities=65536`. Capability bit 17 remained clear.

The final enhanced collector passed without Provider fatal exception, null receiver, or invalid
JNI local-reference diagnostics.

## Boundary

This closes the current-device restart/republication sub-gate only. Mainline artifact identity
change, provenance journal recovery across restart, and actual virtual query/insert/FD/reverse
mapping remain unsupported/not_observed; V-66 and bit 17 remain open.
