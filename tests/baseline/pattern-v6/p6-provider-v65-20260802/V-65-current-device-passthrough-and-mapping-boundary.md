# V-65 current-device Provider boundary

## Device scope

- Device: Xiaomi M2012K11AC (alioth), Android 13/API 33, MIUI
  `V14.0.8.0.TKHCNXM`
- Module: installed `0.1.29-dev` LSPlant bridge
- Collection: `tests/device/provider-contract/collect_lsplant_bridge_status.ps1`
- Evidence: `build/device-evidence/provider-lsplant-v1/20260802-095957`

## Observed passthrough sub-gate

- ExternalStorageProvider: `resolved=3 installed=3 backup=3 self_tested=3 errno=0`
- MediaProvider: `resolved=2044 installed=2044 backup=2044 self_tested=2044 errno=0`
- Both Provider build/profile/library/LSPlant/DEX gates matched.
- Collection exited successfully with the enhanced Provider fault checks enabled.
- Runtime status for both processes reports `action_total=2`, both admissions `active`,
  `observed_capabilities=65536`; `provider_query_insert_mapping` bit 17 remains clear.

## Mapping boundary

The current public probe can create and remove ordinary MediaStore/SAF objects, but it cannot
inject a PathGuard route provenance binding into the production Provider callback. The installed
bridge is intentionally passthrough and does not rewrite URI, document ID, query cursor, insert
result, ParcelFileDescriptor, rename/delete target, or reverse lookup.

Therefore the following V-65 sub-scenarios are `unsupported/not_observed` on this device and are
not counted as passed:

- virtual query/create/insert result rewriting;
- document ID and relative-path rewriting;
- open FD identity proving the same route record;
- rename/delete forward and reverse mapping;
- MediaStore scan target-to-source attribution;
- ambiguous reverse and injected fail-open behavior at the Java callback boundary.

## Conclusion

The LSPlant passthrough sub-gate is observed again on the current device. V-65 remains `pending`
because the real composite mapping matrix is not constructible with the current probe/production
configuration. Bit 17 remains `unsupported`.
