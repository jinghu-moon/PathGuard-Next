# Comparison report fixtures

`valid-report.json` is the canonical format 1 report template and positive fixture.

Field names, field groups and classification values are defined once in
`tests/baseline/comparison_report_schema_v1.cmake`. Tests and the validator include that manifest rather than
copying schema constants. Historical Markdown evidence remains read-only and is not format 1 input.
