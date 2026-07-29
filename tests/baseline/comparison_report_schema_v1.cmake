set(PATHGUARD_COMPARISON_REPORT_FORMAT 1)

set(PATHGUARD_COMPARISON_REPORT_STRING_FIELDS
  change_id
  before_commit
  after_commit
  rules_source_hash
  policy_hash
  scenario_id
  expected_result
  before_actual
  after_actual
  classification
  reviewer_conclusion
)

set(PATHGUARD_COMPARISON_REPORT_OBJECT_FIELDS
  environment
  module_abi_hashes
)

set(PATHGUARD_COMPARISON_REPORT_ARRAY_FIELDS
  steps
  evidence_paths
)

set(PATHGUARD_COMPARISON_REPORT_REQUIRED_FIELDS
  comparison_report_format
  ${PATHGUARD_COMPARISON_REPORT_STRING_FIELDS}
  ${PATHGUARD_COMPARISON_REPORT_OBJECT_FIELDS}
  ${PATHGUARD_COMPARISON_REPORT_ARRAY_FIELDS}
)

set(PATHGUARD_COMPARISON_REPORT_CLASSIFICATIONS
  unchanged
  planned_break
  unexpected_regression
  not_observed
)
