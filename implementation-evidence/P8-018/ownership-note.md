# Ownership / lifetime note

- Compatibility probe creates a temporary VideoWriter only to verify creatability,
  then cancels and deletes the temp directory.
- No shared queues or cross-thread callbacks are introduced.
- Matrix report contains version strings only (no host/user/hardware UUID).
