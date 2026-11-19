# Ownership / lifetime note

- Complexity measurement is pure value-type inspection of `SceneDocument`
  (no heap growth beyond temporary maps bounded by node count).
- `check_scene_complexity` returns `resource_budget_exceeded` without
  allocating runtime render buffers.
- Compile path looks up the tier by id after graph validation and fails
  closed before further compilation work. Empty `complexity_tier_id`
  disables the check for callers that opt out.
- No threads, queues, or shared mutable state in this path.
