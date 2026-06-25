# ArchAgent

**AI-Assisted Code Modification Engine for C Projects**

---

> This repository contains the Extension developed for Imperial College London's Computing Practical project.
>
> The core coursework (Parts I–III) — an ARMv8-A64 assembler and emulator — forms part of assessed university material and is therefore proprietary and cannot be published.
>
> This repository contains the complete Extension developed by our team, together with all code required to build, run and evaluate the extension independently.

---

## Overview

ArchAgent is a systems-level tool written in C that translates natural-language change requests into validated, tested patches for C codebases. It orchestrates a multi-stage pipeline:

1. Scan a project and build a structured file index
2. Select the most relevant files to fit within a context budget
3. Assemble a structured prompt from system profile + context + request
4. Call a local or remote LLM backend to produce a unified diff
5. Parse, validate, and apply the diff into an isolated sandbox
6. Build and test in the sandbox; surface structured JSON results
7. Optionally apply the patch back to the original project

A second, self-contained sub-system — the **C-to-A64 Playground** — compiles a minimal subset of C (C-Mini) directly to ARMv8 assembly through a hand-written recursive-descent compiler, then assembles and executes the result using the companion assembler and emulator.

---

## Motivation

Existing AI coding tools either call proprietary cloud APIs or require heavyweight language-server infrastructure. ArchAgent is deliberately minimal: a single statically-linked C binary with no runtime dependencies beyond the compiler toolchain. It can run on any POSIX host, use a local quantised model, and fits easily into a CI pipeline.

The project also serves as a concrete demonstration that a complete AI-assisted development workflow — prompt construction, patch extraction, sandboxed validation, and structured reporting — can be implemented entirely in portable C17 without a framework.

---

## Architecture

```
Natural Language Request
         │
         ▼
   ┌─────────────┐
   │ Project     │  project_scanner   — discovers sources, headers,
   │ Scanner     │                      Makefiles and tests
   └──────┬──────┘
          │
          ▼
   ┌─────────────┐
   │ Context     │  context_packer    — selects relevant files
   │ Packer      │                      within a byte budget
   └──────┬──────┘
          │
          ▼
   ┌─────────────┐
   │ Prompt      │  prompt_builder    — assembles system profile,
   │ Builder     │                      context, and request
   └──────┬──────┘
          │
          ▼
   ┌─────────────────────────────┐
   │         LLM Backend         │
   │  mock │ llama.cpp │ Ollama  │
   └──────────────┬──────────────┘
                  │
                  ▼
   ┌─────────────┐
   │ Response    │  response_parser   — extracts plan, patch, tests
   │ Parser      │
   └──────┬──────┘
          │
          ▼
   ┌─────────────┐
   │ Diff        │  diff_parser       — parses unified diff
   │ Parser      │                      into structured hunks
   └──────┬──────┘
          │
          ▼
   ┌─────────────┐
   │ Patch       │  patch_validator   — checks paths, traversal,
   │ Validator   │                      and write-outside-root
   └──────┬──────┘
          │
          ▼
   ┌─────────────┐
   │ Sandbox     │  sandbox           — isolated session directory
   │             │                      with timestamped ID
   └──────┬──────┘
          │
          ▼
   ┌─────────────┐
   │ Patch       │  patch_applier     — fuzzy-match hunk
   │ Applier     │                      application
   └──────┬──────┘
          │
          ▼
   ┌─────────────┐
   │ Command     │  command_runner    — allowlist-guarded shell
   │ Runner      │                      with timeout & capture
   └──────┬──────┘
          │
          ▼
   ┌─────────────┐
   │ Audit       │  audit_logger      — JSONL event log +
   │ Logger      │                      per-session artefacts
   └─────────────┘
```

### C-to-A64 Playground (sub-pipeline)

```
C-Mini Source
     │
     ▼  c2asm_lexer    — hand-written tokeniser
     │
     ▼  c2asm_parser   — recursive-descent parser → AST
     │
     ▼  c2asm_codegen  — AST → ARMv8 A64 assembly text
     │
     ▼  assemble       — coursework assembler binary
     │
     ▼  emulate        — coursework emulator binary
     │
     ▼
Return value (X0 register) + session artefacts
```

---

## Features

| Feature | Detail |
|---------|--------|
| **Three LLM backends** | `mock` (built-in, no deps), `llama` (local llama.cpp), `ollama` (HTTP API) |
| **Sandboxed execution** | All changes happen in an isolated session directory; original is never touched unless `--apply` is passed |
| **Patch safety** | Directory-traversal checks, write-outside-root rejection, path canonicalisation |
| **Fuzzy patch application** | Tolerates minor whitespace drift in context lines |
| **Structured JSON output** | Every stage emits a machine-readable result suitable for CI integration |
| **Audit trail** | Per-session artefacts: prompt, raw response, diff, build/test logs, result.json |
| **C-Mini compiler** | Lexer + recursive-descent parser + A64 code generator for a minimal C subset |
| **Flask web UI** | Browser-based interface to the full pipeline |
| **14 unit tests** | Covering every module independently |
| **Three demo projects** | Calculator, word-count, matrix — each with a canned mock-backend scenario |

---

## Repository Layout

```
archagent/
├── src/                    # 20 C implementation files
│   ├── main.c              # CLI entry point & pipeline orchestration
│   ├── config.c            # Argument parsing
│   ├── target_profile.c    # Host system fingerprinting
│   ├── project_scanner.c   # Recursive file discovery
│   ├── context_packer.c    # Budget-aware context selection
│   ├── prompt_builder.c    # Prompt assembly
│   ├── response_parser.c   # LLM response extraction
│   ├── diff_parser.c       # Unified diff parser
│   ├── patch_validator.c   # Safety checks
│   ├── patch_applier.c     # Fuzzy patch application
│   ├── sandbox.c           # Session isolation
│   ├── command_runner.c    # Allowlist-guarded execution
│   ├── audit_logger.c      # JSONL event logging
│   ├── mock_backend.c      # Built-in canned backend
│   ├── llama_backend.c     # llama.cpp subprocess wrapper
│   ├── ollama_backend.c    # Ollama HTTP API client
│   ├── c2asm_pipeline.c    # C-to-A64 pipeline orchestration
│   ├── c2asm_lexer.c       # C-Mini tokeniser
│   ├── c2asm_parser.c      # C-Mini recursive-descent parser
│   └── c2asm_codegen.c     # A64 assembly code generator
├── include/                # Corresponding header files (one per module)
├── tests/                  # 14 unit test programs
├── examples/               # C-Mini example programs (.cmini)
├── demo_projects/
│   ├── calculator/         # Demo: add exponentiation to a calculator
│   ├── wordcount/          # Demo: fix whitespace handling in word counter
│   └── matrix/             # Demo: optimise matrix traversal for cache locality
├── gui/                    # Flask web UI
│   ├── app.py
│   ├── templates/
│   └── static/
├── docs/
│   └── architecture.md     # Detailed module documentation
├── Makefile
├── LICENSE
└── CONTRIBUTING.md
```

---

## Build

### Requirements

- C17-capable compiler: `gcc` ≥ 9 or `clang` ≥ 11
- GNU Make
- POSIX-compliant OS (Linux, macOS)
- Python 3 and Flask (optional, for the web UI only)

No other runtime dependencies are required for the core binary.

```sh
git clone https://github.com/your-org/archagent.git
cd archagent
make
```

The binary is placed at `bin/archagent`.

```sh
# Run the full test suite
make test

# Run only the C-to-A64 compiler tests
make test-c2asm

# Clean all build artefacts
make clean
```

---

## Usage

### AI patch workflow (default mode)

```sh
# Use the built-in mock backend (no external model needed)
./bin/archagent \
  --project demo_projects/calculator \
  --request "Add exponentiation support using ^" \
  --backend mock \
  --yes --json
```

```sh
# Use a local quantised model via llama.cpp
./bin/archagent \
  --project my_project/ \
  --request "Replace the linear search with binary search" \
  --backend llama \
  --model /path/to/qwen2.5-coder-7b-q4.gguf \
  --build-cmd "make" \
  --test-cmd "make test" \
  --yes
```

```sh
# Use Ollama (requires Ollama running locally)
./bin/archagent \
  --project my_project/ \
  --request "Add input validation to all public functions" \
  --backend ollama \
  --model qwen2.5-coder:7b \
  --build-cmd "make" \
  --test-cmd "make test"
```

### Inspect a project without making changes

```sh
# Print host system profile used to seed prompts
./bin/archagent --profile

# List all files that would be indexed
./bin/archagent --scan --project my_project/
```

### C-to-A64 Playground

```sh
# Compile inline C-Mini and execute via the assembler/emulator
./bin/archagent \
  --c2asm-code "int n = 5; int r = 1; while (n > 1) { r = r * n; n = n - 1; } return r;" \
  --json

# Compile a .cmini file and print only the generated assembly
./bin/archagent \
  --c2asm examples/c2asm_factorial.cmini \
  --emit-asm-only
```

Example `.cmini` program (`examples/c2asm_factorial.cmini`):

```c
// Factorial of 5 = 120
int n = 5;
int result = 1;

while (n > 1) {
    result = result * n;
    n = n - 1;
}

return result;
```

### Web UI

```sh
make gui
# Open http://127.0.0.1:5050 in a browser
```

### Demo targets

```sh
make demo-calculator   # Mock: add exponentiation to a calculator
make demo-wordcount    # Mock: fix whitespace handling
make demo-matrix       # Mock: optimise matrix traversal + benchmark
make demo-c2asm        # C-Mini: compile and run factorial
```

---

## LLM Backends

| Backend | Flag | Requirements | Notes |
|---------|------|--------------|-------|
| `mock` | `--backend mock` | None | Built-in canned responses; ideal for demos and CI |
| `llama` | `--backend llama --model /path/to/model.gguf` | [llama.cpp](https://github.com/ggml-org/llama.cpp) on `$PATH` | Runs fully offline; no data leaves the machine |
| `ollama` | `--backend ollama --model qwen2.5-coder:7b` | [Ollama](https://ollama.com) running locally | Convenient local HTTP API |

**Model weight files (`.gguf`) are not included in this repository.** Download them separately from a source you are licensed to use.

### llama.cpp setup

```sh
# Install llama.cpp (example: Homebrew on macOS)
brew install llama.cpp

# Or build from source
git clone https://github.com/ggml-org/llama.cpp && cd llama.cpp && make

# Download a model (example: Qwen2.5-Coder 7B Q4)
# Keep it outside the repository
wget https://huggingface.co/... -O ~/models/qwen2.5-coder-7b-q4.gguf

./bin/archagent \
  --project my_project/ \
  --request "..." \
  --backend llama \
  --model ~/models/qwen2.5-coder-7b-q4.gguf
```

---

## JSON Output

All modes support `--json` for machine-readable output, suitable for CI pipelines:

```json
{
  "ok": true,
  "session_id": "20260614_153021_12345",
  "project": "demo_projects/calculator",
  "backend": "mock",
  "patch_validated": true,
  "sandbox_created": true,
  "build": { "ran": true, "exit_code": 0, "passed": true },
  "tests": { "ran": true, "exit_code": 0, "passed": true },
  "benchmark": { "ran": false },
  "session_dir": ".archagent/sessions/20260614_153021_12345/context",
  "summary": "Patch validated, applied in sandbox, build passed and tests passed."
}
```

Each session directory contains:
- `profile.txt` — host system profile sent to the model
- `prompt.txt` — full prompt
- `model_response.txt` — raw model output
- `patch.diff` — extracted unified diff
- `validation_report.txt` — patch safety report
- `build_stdout.txt` / `build_stderr.txt`
- `test_stdout.txt` / `test_stderr.txt`
- `result.json` — structured outcome
- `events.jsonl` — JSONL event log

---

## C-Mini Language

The C-to-A64 Playground compiles a minimal C subset called **C-Mini**:

| Construct | Example |
|-----------|---------|
| Variable declaration | `int x = 42;` |
| Assignment | `x = x + 1;` |
| Arithmetic | `+`, `-`, `*`, `/` |
| Comparison | `==`, `!=`, `<`, `<=`, `>`, `>=` |
| Conditional | `if (cond) { ... } else { ... }` |
| Loop | `while (cond) { ... }` |
| Return | `return expr;` |

Variables are mapped to ARMv8 registers X1–X15 (up to 15 variables). Temporaries use X16–X25. The generated assembly is valid input to the companion ARMv8 assembler.

---

## Design Decisions

**Why C?** The project targets systems engineers. A C17 implementation demonstrates memory management discipline, explicit ownership, and zero-overhead abstractions. It also avoids the dependency chain that comes with higher-level languages.

**Why sandboxing?** The pipeline applies model-generated patches. A sandbox means a single incorrect patch cannot corrupt the original project. All build and test commands run inside the session copy, with an explicit `--apply` flag required to touch the original.

**Why a hand-written parser?** The C-Mini compiler uses a recursive-descent parser rather than a parser generator (like yacc). This keeps the dependency count at zero and makes the parser easier to trace and debug — important when error messages must point to exact source locations.

**Why a fuzzy patch applier?** LLMs sometimes emit diffs with slightly wrong line numbers or minor whitespace differences in context lines. The applier uses a tolerance window to find the correct hunk position rather than failing on small mismatches.

**Why three backends?** The mock backend enables fully offline development and CI without any model. The llama backend supports fully private operation with a local model. The Ollama backend offers a convenient HTTP API without manual llama.cpp compilation.

---

## Module Documentation

See [docs/architecture.md](docs/architecture.md) for detailed module descriptions, data structures, and API contracts.

---

## Future Work

- **Incremental context selection**: weight files by edit distance from previous sessions rather than simple byte budgets
- **Multi-file patch coordination**: validate cross-file hunks as a transaction before applying any
- **Streaming backend**: pipe model output token-by-token to reduce latency on long generations
- **LLVM IR backend for C-Mini**: extend the playground to emit LLVM IR in addition to A64 assembly
- **Tree-sitter integration**: use a proper parse tree for more accurate context selection
- **GitHub Actions integration**: publish a reusable workflow action that runs ArchAgent checks on PRs

---

## License

MIT. See [LICENSE](LICENSE).
