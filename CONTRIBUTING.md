# Contributing to ArchAgent

Thank you for your interest in contributing. This document covers how to get started, what the codebase expects, and how to submit changes.

---

## Getting Started

```sh
git clone https://github.com/your-org/archagent.git
cd archagent
make
make test
```

All 14 unit tests must pass before submitting a pull request.

---

## Code Style

ArchAgent is written in **C17** (`-std=c17`). The compiler flags used are:

```
-Wall -Wextra -Werror -pedantic
```

All contributions must compile clean with zero warnings under these flags with both `gcc` and `clang`.

**Formatting conventions:**

- 4-space indentation (no tabs in source files)
- `snake_case` for function and variable names
- `UPPER_CASE` for macros and constants
- Opening braces on the same line as the construct (`K&R` style)
- Function prototypes in the corresponding header, not in `.c` files
- One public module per `.c`/`.h` pair; keep internal helpers `static`

---

## Project Structure

Each module has a single `.c` implementation file and a matching `.h` header:

| Module | Responsibility |
|--------|---------------|
| `config` | CLI argument parsing |
| `target_profile` | Host system fingerprinting |
| `project_scanner` | Recursive project file discovery |
| `context_packer` | Budget-aware context selection |
| `prompt_builder` | Prompt assembly |
| `response_parser` | LLM response extraction |
| `diff_parser` | Unified diff parsing |
| `patch_validator` | Patch safety checks |
| `patch_applier` | Fuzzy patch application |
| `sandbox` | Session isolation |
| `command_runner` | Allowlist-guarded subprocess execution |
| `audit_logger` | JSONL event logging |
| `*_backend` | LLM backend adapters |
| `c2asm_*` | C-Mini compiler and A64 code generator |

---

## Adding a New Backend

1. Create `src/<name>_backend.c` and `include/<name>_backend.h`
2. Implement the function signature:
   ```c
   bool <name>_backend_generate(const char *prompt,
                                const char *model,
                                int timeout_seconds,
                                ModelResponse *out);
   ```
3. Add the backend selection branch in `src/main.c`
4. Document the new backend in `README.md`

---

## Tests

Tests live in `tests/`. Each test file is a self-contained C program that links against the library objects (everything except `main.c`) and returns 0 on success.

```sh
# Run all tests
make test

# Run C-to-A64 compiler tests only
make test-c2asm
```

When adding a module, add a corresponding `tests/test_<module>.c`. Use the lightweight helpers in `tests/test_helpers.h`.

---

## Pull Requests

1. Fork the repository and work on a feature branch
2. Ensure `make test` passes
3. Keep commits focused and well-described
4. Reference any relevant issue in the PR description
5. Do not commit model weight files (`.gguf`, `.bin`)
6. Do not commit session artefacts (`.archagent/` directories)

---

## Reporting Issues

Please use GitHub Issues. Include:

- Operating system and compiler version
- Steps to reproduce
- Expected vs actual behaviour
- Any relevant output from `--json` mode

---

## Licence

By contributing, you agree that your contributions will be licensed under the MIT License.
