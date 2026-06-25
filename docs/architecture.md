# ArchAgent — Architecture and Module Reference

This document describes every module in the ArchAgent codebase: its purpose, public API, key data structures, and design rationale.

---

## Top-Level Entry Point

### `src/main.c`

The CLI entry point. Parses arguments via `config_parse()` and dispatches to one of four modes:

| Flag | Mode | Function |
|------|------|----------|
| `--profile` | Print host profile | `run_profile()` |
| `--scan` | List project files | `run_scan()` |
| `--c2asm` / `--c2asm-code` | C-Mini compiler | `run_c2asm()` |
| `--request` / `--request-file` | AI patch pipeline | `run_request()` |

`run_request()` implements the full 17-step pipeline:

```
1.  config_parse()              — validated arguments
2.  sandbox_create()            — session directory
3.  target_profile_build()      — OS/compiler fingerprint
4.  project_scan()              — file index
5.  context_pack_build()        — relevant file selection
6.  prompt_build()              — assembled prompt text
7.  <backend>_generate()        — LLM inference
8.  response_parse()            — plan / patch / tests extraction
9.  diff_parse()                — unified diff → structured hunks
10. patch_validate()            — safety checks
11. sandbox_copy_project()      — copy project into sandbox
12. patch_apply_to_sandbox()    — apply diff to sandbox copy
13. command_run_checked()       — build in sandbox
14. command_run_checked()       — test in sandbox
15. command_run_checked()       — benchmark (optional)
16. write_result_json()         — structured result
17. audit_log_event()           — final JSONL event
```

---

## Configuration

### `src/config.c` / `include/config.h`

Parses `argv` into a `Config` struct. All fields are zero-initialised before parsing; `config_parse()` returns `false` and prints a usage message on any error.

```c
typedef struct {
    const char *project_path;       // --project
    const char *request_text;       // --request
    const char *request_file;       // --request-file
    const char *backend;            // --backend (mock|llama|ollama)
    const char *model;              // --model <path or name>
    const char *build_cmd;          // --build-cmd
    const char *test_cmd;           // --test-cmd
    const char *bench_cmd;          // --bench-cmd
    const char *audit_dir;          // --audit-dir (default: .archagent)
    int         timeout_seconds;    // --timeout (default: 10)
    size_t      max_context_bytes;  // --max-context (default: 30720)
    bool        apply_to_original;  // --apply
    bool        json;               // --json
    bool        profile_only;       // --profile
    bool        scan_only;          // --scan
    bool        c2asm_mode;         // --c2asm / --c2asm-code
    const char *c2asm_file;         // --c2asm <file>
    const char *c2asm_code;         // --c2asm-code <inline>
    const char *c2asm_assemble_bin; // --assemble-bin
    const char *c2asm_emulate_bin;  // --emulate-bin
    const char *c2asm_repo_root;    // --repo-root
    bool        c2asm_emit_asm_only;// --emit-asm-only
    bool        c2asm_keep_artifacts;
    bool        verbose;
} Config;
```

---

## Project Analysis

### `src/target_profile.c` / `include/target_profile.h`

Builds a human-readable host profile to include in the model prompt. Collects:

- Operating system name and version (`uname -s`, `uname -r`)
- Compiler identity and version (`cc --version`)
- CPU architecture (`uname -m`)

```c
typedef struct {
    char text[4096]; // free-form profile text, NUL-terminated
} TargetProfile;

bool target_profile_build(TargetProfile *out);
void target_profile_free(TargetProfile *profile);
```

### `src/project_scanner.c` / `include/project_scanner.h`

Walks a directory tree recursively, classifying each file:

| Field | Set when |
|-------|----------|
| `is_source` | `.c` extension |
| `is_header` | `.h` extension |
| `is_makefile` | filename matches `[Mm]akefile` |
| `is_test` | filename contains `test` or `spec` |

Skips: `.git/`, `.archagent/`, `build/`, `bin/`, `dist/`, `node_modules/`, `__pycache__/`.

Excludes binary file types: `.o`, `.a`, `.so`, `.dylib`, `.exe`, `.bin`, `.gguf`.

```c
typedef struct {
    char   relative_path[PATH_MAX];
    size_t size_bytes;
    bool   is_source, is_header, is_makefile, is_test;
} ProjectFile;

typedef struct {
    ProjectFile *files;
    size_t       count;
} ProjectIndex;

bool project_scan(const char *root, ProjectIndex *out);
void project_index_free(ProjectIndex *index);
```

---

## Prompt Construction

### `src/context_packer.c` / `include/context_packer.h`

Selects files to include in the model context within a byte budget (`max_context_bytes`, default 30 KB). Priority order:

1. Makefiles (always included first)
2. Files whose name or content appears relevant to the request
3. Source files, then headers
4. Files are truncated if the budget is nearly exhausted

```c
typedef struct {
    char  *text;       // packed context text, malloc'd
    size_t used_bytes;
    size_t file_count;
} ContextPack;

bool context_pack_build(const char *project_root,
                        const ProjectIndex *index,
                        const char *request,
                        size_t max_bytes,
                        ContextPack *out);
void context_pack_free(ContextPack *pack);
```

### `src/prompt_builder.c` / `include/prompt_builder.h`

Assembles the final prompt from three components:

```
<SYSTEM_PROFILE>
{target profile text}
</SYSTEM_PROFILE>

<PROJECT_CONTEXT>
{packed context: Makefile + relevant source files}
</PROJECT_CONTEXT>

<CHANGE_REQUEST>
{user's natural-language request}
</CHANGE_REQUEST>
```

Instructs the model to respond with three delimited sections:
`<PLAN>`, `<PATCH>` (unified diff), `<TESTS>`.

```c
typedef struct {
    char *text;  // malloc'd
} Prompt;

bool prompt_build(const TargetProfile *profile,
                  const ContextPack *context,
                  const char *request,
                  Prompt *out);
void prompt_free(Prompt *prompt);
```

---

## LLM Backends

All three backends implement the same interface:

```c
typedef struct {
    char *text;       // malloc'd model output
    int   exit_code;
    bool  timed_out;
} ModelResponse;

// Mock backend — canned responses, no external dependencies
bool mock_backend_generate(const char *request, ModelResponse *out);

// llama.cpp backend — spawns llama-cli subprocess
bool llama_backend_generate(const char *prompt,
                             const char *model_path,
                             int timeout_seconds,
                             ModelResponse *out);

// Ollama backend — HTTP POST to localhost:11434
bool ollama_backend_generate(const char *prompt,
                              const char *model_name,
                              int timeout_seconds,
                              ModelResponse *out);

void model_response_free(ModelResponse *response);
```

**Mock backend** (`src/mock_backend.c`): Contains canned responses keyed on keywords in the request string. Designed for demos and CI where no model is available.

**llama backend** (`src/llama_backend.c`): Writes the prompt to a temporary file, then `fork()`/`execv()`s `llama-cli` with `--file` and `--model` flags. Captures stdout. Returns exit code 127 if `llama-cli` is not found on `$PATH`.

**Ollama backend** (`src/ollama_backend.c`): Makes an HTTP POST to `http://localhost:11434/api/generate`. Parses the JSONL streaming response, accumulating `response` fields. Handles connection refused, timeouts, and HTTP errors with descriptive error messages.

---

## Response Parsing

### `src/response_parser.c` / `include/response_parser.h`

Extracts three sections from the model's raw text using delimiter scanning:

```c
typedef struct {
    char *plan_text;   // malloc'd — model's explanation of changes
    char *patch_text;  // malloc'd — unified diff
    char *tests_text;  // malloc'd — test/verify commands
} ParsedResponse;

bool response_parse(const char *text, ParsedResponse *out);
void parsed_response_free(ParsedResponse *parsed);
```

Delimiters searched for (case-insensitive, with variations):
- `<PLAN>` ... `</PLAN>`
- `<PATCH>` / ` ```diff ` ... `</PATCH>` / ` ``` `
- `<TESTS>` ... `</TESTS>`

---

## Patch Safety and Application

### `src/diff_parser.c` / `include/diff_parser.h`

Parses a unified diff string into a structured representation:

```c
typedef enum { DIFF_CONTEXT, DIFF_ADD, DIFF_DEL } DiffLineType;

typedef struct {
    DiffLineType type;
    char        *text;  // malloc'd line content (without leading +/-/ )
} DiffLine;

typedef struct {
    int       old_start, old_count;
    int       new_start, new_count;
    DiffLine *lines;
    size_t    line_count;
} DiffHunk;

typedef struct {
    char      old_path[PATH_MAX];
    char      new_path[PATH_MAX];
    DiffHunk *hunks;
    size_t    hunk_count;
} FilePatch;

typedef struct {
    FilePatch *files;
    size_t     file_count;
} ParsedDiff;

bool diff_parse(const char *text, ParsedDiff *out);
void parsed_diff_free(ParsedDiff *diff);
```

### `src/patch_validator.c` / `include/patch_validator.h`

Validates a parsed diff before any files are written:

- Rejects paths containing `..` (directory traversal)
- Rejects absolute paths
- Verifies no file would be written outside the project root
- Rejects empty diffs

```c
typedef struct {
    bool ok;
    char message[1024];
} ValidationReport;

bool patch_validate(const ParsedDiff *diff,
                    const char *project_root,
                    ValidationReport *out);
```

### `src/patch_applier.c` / `include/patch_applier.h`

Applies a parsed diff to a sandbox directory with fuzzy matching:

- Reads the target file into memory
- For each hunk, searches for context lines within a tolerance window (±5 lines by default) to handle off-by-one line numbers from the model
- Whitespace in context lines is normalised for matching
- New files are created if the old path is `/dev/null`
- Parent directories are created as needed

```c
typedef struct {
    bool ok;
    char message[1024];
} PatchApplyReport;

bool patch_apply_to_sandbox(const ParsedDiff *diff,
                             const char *sandbox_root,
                             PatchApplyReport *out);
```

---

## Sandbox and Execution

### `src/sandbox.c` / `include/sandbox.h`

Creates an isolated session directory and copies the project into it:

```c
typedef struct {
    char session_id[64];       // "YYYYMMDD_HHMMSS_<pid>"
    char session_dir[PATH_MAX]; // <audit_dir>/sessions/<id>
    char sandbox_root[PATH_MAX];// <session_dir>/context
} Sandbox;

bool sandbox_create(const char *project_root,
                    const char *audit_dir,
                    Sandbox *out);
bool sandbox_copy_project(const char *project_root,
                           const Sandbox *sandbox);
```

The session ID encodes a timestamp and PID to avoid collisions across concurrent runs.

### `src/command_runner.c` / `include/command_runner.h`

Executes shell commands inside the sandbox with safety constraints:

- **Allowlist**: only commands matching known patterns (`make`, `./`, `bash -c`, etc.) are allowed
- **Timeout**: enforced via `fork()`/`SIGKILL` if the child exceeds the limit
- **Capture**: stdout and stderr are captured separately via pipes
- **Working directory**: `chdir()` to `sandbox_root` before exec

```c
typedef struct {
    int   exit_code;
    bool  timed_out;
    char *stdout_text;  // malloc'd
    char *stderr_text;  // malloc'd
} CommandResult;

bool command_run_checked(const char *cwd,
                          const char *cmd,
                          int timeout_seconds,
                          CommandResult *out);
void command_result_free(CommandResult *result);
```

### `src/audit_logger.c` / `include/audit_logger.h`

Writes per-session artefacts and a JSONL event log:

```c
void audit_log_event(const Sandbox *sandbox, const char *json_line);
void audit_write_file(const Sandbox *sandbox,
                      const char *filename,
                      const char *content);
```

Events are appended to `<session_dir>/events.jsonl`. Artefact files are written to `<session_dir>/<filename>`.

---

## C-to-A64 Playground

The playground is a self-contained compiler for C-Mini (a minimal C subset) that produces ARMv8 A64 assembly.

### `src/c2asm_lexer.c` / `include/c2asm_lexer.h`

Hand-written lexer. Tokenises C-Mini source into:

| Token type | Examples |
|------------|---------|
| `TOK_INT` | `int` |
| `TOK_RETURN` | `return` |
| `TOK_IF`, `TOK_ELSE` | `if`, `else` |
| `TOK_WHILE` | `while` |
| `TOK_IDENT` | `result`, `n`, `x` |
| `TOK_NUMBER` | `42`, `0`, `1000` |
| `TOK_OP` | `+`, `-`, `*`, `/`, `=`, `==`, `!=`, `<`, `<=`, `>`, `>=` |
| `TOK_LBRACE` / `TOK_RBRACE` | `{`, `}` |
| `TOK_LPAREN` / `TOK_RPAREN` | `(`, `)` |
| `TOK_SEMI` | `;` |
| `TOK_EOF` | end of input |

Line and column numbers are tracked for error reporting.

```c
typedef struct {
    TokenType   type;
    char        text[256];
    int         line, col;
} Token;

typedef struct {
    const char *src;
    size_t      pos;
    int         line, col;
    char        error[256];
} Lexer;

void  lexer_init(Lexer *l, const char *source);
Token lexer_next(Lexer *l);
Token lexer_peek(Lexer *l);
```

### `src/c2asm_parser.c` / `include/c2asm_parser.h`

Recursive-descent parser. Produces an AST from the token stream.

**AST node types:**

| Type | Fields used | Meaning |
|------|-------------|---------|
| `AST_PROGRAM` | `stmts[]` | Top-level statement list |
| `AST_DECL` | `name`, `right` | `int name = expr;` |
| `AST_ASSIGN` | `name`, `right` | `name = expr;` |
| `AST_RETURN` | `right` | `return expr;` |
| `AST_IF` | `cond`, `body`, `else_` | `if`/`else` |
| `AST_WHILE` | `cond`, `body` | `while` loop |
| `AST_BLOCK` | `stmts[]` | `{ ... }` |
| `AST_BINOP` | `op`, `left`, `right` | Binary expression |
| `AST_IDENT` | `name` | Variable reference |
| `AST_NUMBER` | `int_val` | Integer literal |

```c
struct AstNode {
    AstNodeType  type;
    char         op[4];
    char         name[256];
    int64_t      int_val;
    AstNode     *left, *right;
    AstNode     *cond, *body, *else_;
    AstNode    **stmts;
    size_t       stmt_count;
};

bool     parser_init(Parser *p, Lexer *l, const char *src, size_t src_len);
AstNode *parser_parse(Parser *p);  // returns NULL on error; check p->error
void     ast_free(AstNode *node);
```

### `src/c2asm_codegen.c` / `include/c2asm_codegen.h`

Walks the AST and emits ARMv8 A64 assembly text.

**Register allocation:**

| Purpose | Registers |
|---------|-----------|
| User variables (up to 15) | X1–X15 |
| Temporaries | X16–X25 |
| Return value | X0 |
| Stack pointer | SP |

**Generated prologue/epilogue:**

```asm
.text
.global _start
_start:
    ; ... variable initialisations ...
    ; ... body ...
    MOV X0, Xn       ; move return value
    MOV X8, #93      ; sys_exit syscall number
    SVC #0
```

Control-flow labels follow the pattern `__if_N_else`, `__if_N_end`, `__while_N_cond`, `__while_N_end`.

```c
typedef struct {
    char  *buf;          // malloc'd assembly text
    size_t len, cap;
    char   error[256];
    // internal: variable → register map, label counter
} Codegen;

bool  codegen_init(Codegen *cg);
bool  codegen_generate(Codegen *cg, AstNode *program);
char *codegen_get_assembly(Codegen *cg);  // transfers ownership
void  codegen_free(Codegen *cg);
```

### `src/c2asm_pipeline.c` / `include/c2asm_pipeline.h`

Orchestrates the full compilation run:

```
source text / .cmini file
    │ lexer_init / lexer_next
    ▼
Token stream
    │ parser_parse
    ▼
AST
    │ codegen_generate
    ▼
Assembly text  ──── written to session_dir/generated.s
    │ execv(assemble_bin)
    ▼
Binary          ──── written to session_dir/program.bin
    │ execv(emulate_bin)
    ▼
Emulator output ──── written to session_dir/emulator.out
    │ parse_x00()
    ▼
uint64_t return value + result.json
```

Tool discovery order (assembler and emulator):
1. Explicit `--assemble-bin` / `--emulate-bin` flag
2. `<repo_root>/src/<tool>`
3. Relative paths: `../../src/`, `../src/`, `src/`, `../../../src/`

```c
typedef struct {
    bool     ok;
    char     stage[32];      // failed stage name, or "emulate" on success
    char     error[1024];
    char     session_id[64];
    char     session_dir[4096];
    char     input_path[4096];
    char     assembly_path[4096];
    char     binary_path[4096];
    char     emulator_output_path[4096];
    char     result_json_path[4096];
    char    *generated_assembly;   // malloc'd
    char    *emulator_output;      // malloc'd
    uint64_t return_value;
    bool     return_value_available;
} C2AsmResult;

bool c2asm_pipeline_run(const C2AsmOptions *opts, C2AsmResult *out);
void c2asm_result_free(C2AsmResult *result);
void c2asm_result_print_json(const C2AsmResult *result,
                              const C2AsmOptions *opts);
```

---

## Web UI

`gui/app.py` is a Flask application that wraps the CLI binary with a browser interface. It:

- Lists demo projects and user-supplied projects
- Accepts change requests through a form
- Calls `./bin/archagent ... --json` as a subprocess
- Parses the JSON output and renders session results
- Provides a separate view for the C-to-A64 playground

Requires Python 3 and the packages listed in `gui/requirements.txt` (`flask`).

---

## Data Flow Summary

```
User request (string)
    │
    ├─► project_scan()        →  ProjectIndex  (file list)
    │
    ├─► context_pack_build()  →  ContextPack   (selected text)
    │
    ├─► prompt_build()        →  Prompt        (assembled text)
    │
    ├─► <backend>_generate()  →  ModelResponse (raw text)
    │
    ├─► response_parse()      →  ParsedResponse (plan + diff + tests)
    │
    ├─► diff_parse()          →  ParsedDiff    (structured hunks)
    │
    ├─► patch_validate()      →  ValidationReport
    │
    ├─► sandbox_create()      →  Sandbox       (session directory)
    │   sandbox_copy_project()
    │
    ├─► patch_apply_to_sandbox() →  PatchApplyReport
    │
    ├─► command_run_checked()    →  CommandResult  (build)
    │   command_run_checked()    →  CommandResult  (test)
    │
    └─► audit_write_file()    →  result.json, events.jsonl
```
