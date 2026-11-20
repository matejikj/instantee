# clang-tidy pin

Xcode does not ship `clang-tidy`. Local and CI analysis uses the Homebrew
`llvm` formula:

| Field | Value |
|---|---|
| Package | `llvm` (Homebrew) |
| Version | `23.1.5` |
| Executable | `$(brew --prefix llvm)/bin/clang-tidy` |
| Config | repository-root `.clang-tidy` |

Install or upgrade only that formula when refreshing the pin:

```sh
brew install llvm
export CLANG_TIDY="$(brew --prefix llvm)/bin/clang-tidy"
"$CLANG_TIDY" --version
```

`tools/dev/check.py` honors `CLANG_TIDY` and treats a missing binary as
`incomplete`, not `passed`. Findings in changed project translation units fail
the check through `--warnings-as-errors=*`. Release-gate CI runs
`tools/dev/run_clang_tidy_own.py` over every own translation unit present in
`compile_commands.json`. System Apple frameworks and `third_party/` headers stay
outside `HeaderFilterRegex`.
