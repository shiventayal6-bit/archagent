# Created by sm4925 on 2026/6/11

from __future__ import annotations

import json
import os
import re
import shlex
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from flask import Flask, Response, abort, flash, redirect, render_template, request, url_for


GUI_DIR = Path(__file__).resolve().parent
ARCHAGENT_ROOT = GUI_DIR.parent
ARCHAGENT_BIN = ARCHAGENT_ROOT / "bin" / "archagent"

DEFAULT_BACKEND = "mock"
DEFAULT_BUILD_CMD = "make"
DEFAULT_TEST_CMD = "make test"
DEFAULT_TIMEOUT_SECONDS = 120
DEFAULT_MAX_CONTEXT_BYTES = 30000

MAX_REQUEST_CHARS = 10000
MAX_DISPLAY_FILE_BYTES = 750_000

ALLOWED_BACKENDS = {"mock", "llama", "ollama"}

SAFE_SESSION_ID_RE = re.compile(r"^[A-Za-z0-9_.-]+$")
SAFE_SESSION_FILE_RE = re.compile(r"^[A-Za-z0-9_.-]+$")

SESSION_FILE_ORDER = [
    "summary.txt",
    "result.json",
    "profile.txt",
    "project_index.txt",
    "prompt.txt",
    "model_response.txt",
    "parsed_plan.txt",
    "patch.diff",
    "validation_report.txt",
    "build_stdout.txt",
    "build_stderr.txt",
    "test_stdout.txt",
    "test_stderr.txt",
    "benchmark.txt",
    "events.jsonl",
]
SESSION_FILE_SET = set(SESSION_FILE_ORDER)

DEMO_PROJECTS = [
    {
        "name": "Calculator",
        "path": "demo_projects/calculator",
        "description": "Feature addition demo: add exponentiation support using the ^ operator.",
        "default_request": "Add exponentiation support using ^.",
        "badge": "Feature addition",
    },
    {
        "name": "Wordcount",
        "path": "demo_projects/wordcount",
        "description": "Bug-fixing demo: correct word counting for repeated spaces and newlines.",
        "default_request": "Fix word counting for multiple spaces and newlines.",
        "badge": "Bug fixing",
    },
    {
        "name": "Matrix",
        "path": "demo_projects/matrix",
        "description": "Architecture-aware optimisation demo: improve row-major cache locality.",
        "default_request": "Optimise matrix traversal for cache locality.",
        "badge": "Benchmarking",
    },
]

EXAMPLE_CMINI_PROGRAMS = {
    "basic": {
        "name": "Basic arithmetic",
        "source": "int a = 10;\nint b = 3;\nint c = a + b * 2;\nreturn c;\n",
    },
    "branch": {
        "name": "If/else maximum",
        "source": "int a = 10;\nint b = 20;\n\nif (a < b) {\n    return b;\n} else {\n    return a;\n}\n",
    },
    "loop": {
        "name": "While loop sum",
        "source": "int n = 10;\nint sum = 0;\n\nwhile (n > 0) {\n    sum = sum + n;\n    n = n - 1;\n}\n\nreturn sum;\n",
    },
    "factorial": {
        "name": "Factorial (5! = 120)",
        "source": "int n = 5;\nint result = 1;\n\nwhile (n > 1) {\n    result = result * n;\n    n = n - 1;\n}\n\nreturn result;\n",
    },
}

EXAMPLE_C_PROGRAMS = {
    "arithmetic": {
        "name": "Arithmetic",
        "source": (
            "#include <stdio.h>\n\n"
            "int main() {\n"
            "    int a = 12;\n"
            "    int b = 4;\n\n"
            "    printf(\"a = %d, b = %d\\n\", a, b);\n"
            "    printf(\"Addition:       %d\\n\", a + b);\n"
            "    printf(\"Subtraction:    %d\\n\", a - b);\n"
            "    printf(\"Multiplication: %d\\n\", a * b);\n"
            "    printf(\"Division:       %d\\n\", a / b);\n"
            "    printf(\"Remainder:      %d\\n\", a %% b);\n"
            "    return 0;\n"
            "}\n"
        ),
    },
    "fibonacci": {
        "name": "Fibonacci",
        "source": (
            "#include <stdio.h>\n\n"
            "int main() {\n"
            "    int a = 0, b = 1, n = 10;\n"
            "    printf(\"First %d Fibonacci numbers:\\n\", n);\n"
            "    for (int i = 0; i < n; i++) {\n"
            "        printf(\"  F(%d) = %d\\n\", i, a);\n"
            "        int t = a + b; a = b; b = t;\n"
            "    }\n"
            "    return 0;\n"
            "}\n"
        ),
    },
    "strings": {
        "name": "Strings",
        "source": (
            "#include <stdio.h>\n"
            "#include <string.h>\n\n"
            "int main() {\n"
            "    char s[] = \"Hello, ArchAgent!\";\n"
            "    printf(\"String:  %s\\n\", s);\n"
            "    printf(\"Length:  %zu\\n\", strlen(s));\n"
            "    printf(\"Upper-H: %c\\n\", s[0]);\n"
            "    return 0;\n"
            "}\n"
        ),
    },
    "recursion": {
        "name": "Recursion (factorial)",
        "source": (
            "#include <stdio.h>\n\n"
            "long factorial(int n) {\n"
            "    return n <= 1 ? 1 : n * factorial(n - 1);\n"
            "}\n\n"
            "int main() {\n"
            "    for (int i = 0; i <= 10; i++)\n"
            "        printf(\"%2d! = %ld\\n\", i, factorial(i));\n"
            "    return 0;\n"
            "}\n"
        ),
    },
}

GCC_BIN: str = shutil.which("gcc") or "gcc"

app = Flask(__name__)
app.secret_key = "dsfhjksdfhkjdsfhkdsjhfsfusdfgkjsdghksdjlhflsjdkzhgkjsdhgjksldhgjklsdhgkjsdhfkjlfgs"


@dataclass
class CommandOutcome:
    command: List[str]
    returncode: int
    stdout: str
    stderr: str
    timed_out: bool = False


def archagent_exists() -> bool:
    """Return true only when the C engine exists and is executable."""
    return ARCHAGENT_BIN.exists() and os.access(str(ARCHAGENT_BIN), os.X_OK)


def relative_to_archagent_root(path: Path) -> str:
    """Return a short path for display while preserving absolute paths outside this extension."""
    try:
        return str(path.resolve().relative_to(ARCHAGENT_ROOT.resolve()))
    except ValueError:
        return str(path.resolve())


def resolve_project_path(raw_path: str) -> Path:
    """
    Resolve a user-selected project directory.

    Relative paths are interpreted from extension/archagent so demo projects can be
    opened using paths such as demo_projects/calculator. Absolute paths are allowed
    because the GUI is intended to support separate local coding projects.
    """
    value = (raw_path or "").strip()
    if not value:
        raise ValueError("Project path is empty.")

    candidate = Path(value).expanduser()
    if not candidate.is_absolute():
        candidate = ARCHAGENT_ROOT / candidate

    resolved = candidate.resolve()

    if not resolved.exists():
        raise ValueError(f"Project path does not exist: {resolved}")

    if not resolved.is_dir():
        raise ValueError(f"Project path is not a directory: {resolved}")

    return resolved


def clamp_int(raw_value: Optional[str], default: int, minimum: int, maximum: int) -> int:
    if raw_value is None or raw_value.strip() == "":
        return default

    try:
        value = int(raw_value)
    except ValueError:
        return default

    return max(minimum, min(maximum, value))


def clamp_size(raw_value: Optional[str], default: int, minimum: int, maximum: int) -> int:
    return clamp_int(raw_value, default, minimum, maximum)


def run_archagent(args: List[str], timeout_seconds: int = DEFAULT_TIMEOUT_SECONDS + 60) -> CommandOutcome:
    """
    Run the C engine safely.

    The GUI never uses shell=True. Every value becomes one argv element, and the C
    engine remains responsible for command validation, patch validation, sandboxing
    and audit logging.
    """
    command = [str(ARCHAGENT_BIN)] + args

    if not archagent_exists():
        return CommandOutcome(
            command=command,
            returncode=127,
            stdout="",
            stderr=(
                f"Could not find executable C engine at {ARCHAGENT_BIN}.\n"
                "Build it first with: cd extension/archagent && make"
            ),
            timed_out=False,
        )

    try:
        completed = subprocess.run(
            command,
            cwd=str(ARCHAGENT_ROOT),
            capture_output=True,
            text=True,
            timeout=timeout_seconds,
            check=False,
        )
        return CommandOutcome(
            command=command,
            returncode=completed.returncode,
            stdout=completed.stdout,
            stderr=completed.stderr,
            timed_out=False,
        )

    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout if isinstance(exc.stdout, str) else (exc.stdout or b"").decode("utf-8", errors="replace")
        stderr = exc.stderr if isinstance(exc.stderr, str) else (exc.stderr or b"").decode("utf-8", errors="replace")
        return CommandOutcome(
            command=command,
            returncode=124,
            stdout=stdout,
            stderr=stderr + f"\nCommand timed out after {timeout_seconds} seconds.",
            timed_out=True,
        )

    except OSError as exc:
        return CommandOutcome(
            command=command,
            returncode=126,
            stdout="",
            stderr=f"Could not execute ArchAgent C engine: {exc}",
            timed_out=False,
        )


def extract_json_object(text: str) -> Optional[Dict[str, Any]]:
    """Extract a JSON object from stdout, tolerating harmless surrounding text."""
    if not text:
        return None

    try:
        loaded = json.loads(text)
        return loaded if isinstance(loaded, dict) else None
    except json.JSONDecodeError:
        pass

    start = text.find("{")
    end = text.rfind("}")
    if start == -1 or end == -1 or end <= start:
        return None

    try:
        loaded = json.loads(text[start : end + 1])
        return loaded if isinstance(loaded, dict) else None
    except json.JSONDecodeError:
        return None


def call_archagent_json(args: List[str], timeout_seconds: int) -> Tuple[Optional[Dict[str, Any]], CommandOutcome, Optional[str]]:
    outcome = run_archagent(args, timeout_seconds=timeout_seconds)
    data = extract_json_object(outcome.stdout)

    if data is None:
        return None, outcome, "ArchAgent did not return valid JSON."

    if outcome.returncode != 0 and "ok" not in data:
        data["ok"] = False

    return data, outcome, None


def command_to_string(command: List[str]) -> str:
    return " ".join(shlex.quote(str(part)) for part in command)


def bytes_to_human(size: Any) -> str:
    if size is None or size == "":
        return "unknown"

    try:
        number = float(size)
    except (TypeError, ValueError):
        return str(size)

    units = ["B", "KB", "MB", "GB", "TB"]
    index = 0
    while number >= 1024 and index < len(units) - 1:
        number /= 1024
        index += 1

    if index == 0:
        return f"{int(number)} {units[index]}"
    return f"{number:.1f} {units[index]}"


def safe_session_id(session_id: str) -> str:
    if not SAFE_SESSION_ID_RE.fullmatch(session_id or ""):
        abort(404)
    return session_id


def safe_session_filename(filename: str) -> str:
    if not SAFE_SESSION_FILE_RE.fullmatch(filename or ""):
        abort(404)
    if filename not in SESSION_FILE_SET:
        abort(404)
    return filename


def session_dir_for(project_path: Path, session_id: str) -> Path:
    safe_id = safe_session_id(session_id)
    base = (project_path / ".archagent" / "sessions").resolve()
    session_dir = (base / safe_id).resolve()

    try:
        session_dir.relative_to(base)
    except ValueError:
        abort(404)

    if not session_dir.exists() or not session_dir.is_dir():
        abort(404)

    return session_dir


def read_text_file_limited(path: Path, limit: int = MAX_DISPLAY_FILE_BYTES) -> Tuple[str, bool]:
    """Read at most limit+1 bytes so very large logs are never loaded fully."""
    if not path.exists() or not path.is_file():
        return "", False

    with path.open("rb") as file_obj:
        data = file_obj.read(limit + 1)

    truncated = len(data) > limit
    if truncated:
        data = data[:limit]

    text = data.decode("utf-8", errors="replace")
    if truncated:
        text += "\n\n[Output truncated by GUI display limit.]"

    return text, truncated


def load_session_files(session_dir: Path) -> Dict[str, str]:
    loaded: Dict[str, str] = {}
    for filename in SESSION_FILE_ORDER:
        path = session_dir / filename
        if path.exists() and path.is_file():
            text, _ = read_text_file_limited(path)
            loaded[filename] = text
    return loaded


def diff_lines(diff_text: str) -> List[Dict[str, str]]:
    lines: List[Dict[str, str]] = []
    for line in diff_text.splitlines():
        if line.startswith("@@"):
            kind = "hunk"
        elif line.startswith("+++") or line.startswith("---"):
            kind = "meta"
        elif line.startswith("+"):
            kind = "add"
        elif line.startswith("-"):
            kind = "remove"
        else:
            kind = "context"
        lines.append({"kind": kind, "text": line})
    return lines


def parse_result_json(text: Optional[str]) -> Dict[str, Any]:
    if not text:
        return {}
    try:
        loaded = json.loads(text)
        return loaded if isinstance(loaded, dict) else {}
    except json.JSONDecodeError:
        return {}


def profile_text_from_json(data: Optional[Dict[str, Any]], fallback: str) -> str:
    if not data:
        return fallback
    for key in ("profile_text", "profile", "text", "target_profile"):
        value = data.get(key)
        if isinstance(value, str) and value.strip():
            return value
    return fallback


def scan_files_from_json(data: Optional[Dict[str, Any]]) -> List[Dict[str, Any]]:
    if not data:
        return []

    candidates = data.get("files")
    if candidates is None:
        candidates = data.get("project_index")

    if not isinstance(candidates, list):
        return []

    clean_files: List[Dict[str, Any]] = []
    for item in candidates:
        if isinstance(item, dict):
            clean_files.append(item)
        elif isinstance(item, str):
            clean_files.append({"path": item})
    return clean_files


def value_or_unknown(value: Any) -> Any:
    return value if value is not None and value != "" else "unknown"


@app.context_processor
def inject_global_template_values() -> Dict[str, Any]:
    return {
        "app_name": "ArchAgent IDE",
        "archagent_root": str(ARCHAGENT_ROOT),
        "archagent_bin": str(ARCHAGENT_BIN),
        "archagent_bin_exists": archagent_exists(),
        "demos": DEMO_PROJECTS,
    }


@app.template_filter("human_bytes")
def human_bytes_filter(value: Any) -> str:
    return bytes_to_human(value)


@app.template_filter("command")
def command_filter(command: List[str]) -> str:
    return command_to_string(command)


@app.get("/")
def index() -> str:
    profile_text = None
    profile_error = None
    profile_stdout = ""
    profile_stderr = ""

    if request.args.get("profile") == "1":
        data, outcome, error = call_archagent_json(
            ["--profile", "--json"],
            timeout_seconds=DEFAULT_TIMEOUT_SECONDS,
        )
        profile_text = profile_text_from_json(data, outcome.stdout)
        profile_error = error
        profile_stdout = outcome.stdout
        profile_stderr = outcome.stderr

        if outcome.returncode != 0:
            profile_error = profile_error or f"Profile command failed with exit code {outcome.returncode}."

    return render_template(
        "index.html",
        profile_text=profile_text,
        profile_error=profile_error,
        profile_stdout=profile_stdout,
        profile_stderr=profile_stderr,
    )


@app.get("/project")
def project() -> str:
    raw_path = request.args.get("path", "")
    prefill_request = request.args.get("request", "")

    try:
        project_path = resolve_project_path(raw_path)
    except ValueError as exc:
        flash(str(exc), "error")
        return redirect(url_for("index"))

    scan_data, scan_outcome, scan_error = call_archagent_json(
        ["--project", str(project_path), "--scan", "--json"],
        timeout_seconds=DEFAULT_TIMEOUT_SECONDS,
    )

    profile_data, profile_outcome, profile_error = call_archagent_json(
        ["--profile", "--json"],
        timeout_seconds=DEFAULT_TIMEOUT_SECONDS,
    )

    files = scan_files_from_json(scan_data)
    profile_text = profile_text_from_json(profile_data, profile_outcome.stdout)
    profile_summary = "\n".join(profile_text.splitlines()[:35])

    return render_template(
        "project.html",
        project_path=str(project_path),
        project_display_path=relative_to_archagent_root(project_path),
        prefill_request=prefill_request,
        scan_data=scan_data or {},
        files=files,
        scan_outcome=scan_outcome,
        scan_error=scan_error,
        profile_summary=profile_summary,
        profile_error=profile_error,
        defaults={
            "backend": DEFAULT_BACKEND,
            "build_cmd": DEFAULT_BUILD_CMD,
            "test_cmd": DEFAULT_TEST_CMD,
            "timeout": DEFAULT_TIMEOUT_SECONDS,
            "max_context_bytes": DEFAULT_MAX_CONTEXT_BYTES,
        },
    )


@app.post("/run")
def run_agent() -> str:
    raw_project_path = request.form.get("project_path", "")
    request_text = (request.form.get("request_text", "") or "").strip()
    backend = (request.form.get("backend", DEFAULT_BACKEND) or DEFAULT_BACKEND).strip()
    model = (request.form.get("model", "") or "").strip()
    build_cmd = (request.form.get("build_cmd", DEFAULT_BUILD_CMD) or DEFAULT_BUILD_CMD).strip()
    test_cmd = (request.form.get("test_cmd", DEFAULT_TEST_CMD) or DEFAULT_TEST_CMD).strip()
    bench_cmd = (request.form.get("bench_cmd", "") or "").strip()
    apply_to_original = request.form.get("apply_to_original") == "on"

    engine_timeout = clamp_int(
        request.form.get("timeout_seconds"),
        DEFAULT_TIMEOUT_SECONDS,
        minimum=5,
        maximum=1800,
    )
    max_context_bytes = clamp_size(
        request.form.get("max_context_bytes"),
        DEFAULT_MAX_CONTEXT_BYTES,
        minimum=1000,
        maximum=500000,
    )

    try:
        project_path = resolve_project_path(raw_project_path)
    except ValueError as exc:
        flash(str(exc), "error")
        return redirect(url_for("index"))

    if not request_text:
        flash("Request text cannot be empty.", "error")
        return redirect(url_for("project", path=str(project_path)))

    if len(request_text) > MAX_REQUEST_CHARS:
        flash(f"Request text is too long. Maximum allowed length is {MAX_REQUEST_CHARS} characters.", "error")
        return redirect(url_for("project", path=str(project_path), request=request_text[:500]))

    if backend not in ALLOWED_BACKENDS:
        flash(f"Unsupported backend: {backend}", "error")
        return redirect(url_for("project", path=str(project_path), request=request_text))

    args = [
        "--project",
        str(project_path),
        "--request",
        request_text,
        "--backend",
        backend,
        "--build-cmd",
        build_cmd,
        "--test-cmd",
        test_cmd,
        "--timeout",
        str(engine_timeout),
        "--max-context-bytes",
        str(max_context_bytes),
        "--yes",
        "--json",
    ]

    if model:
        args.extend(["--model", model])
    if bench_cmd:
        args.extend(["--bench-cmd", bench_cmd])
    if apply_to_original:
        args.append("--apply")

    subprocess_timeout = max(engine_timeout + 60, 90)
    result_data, outcome, json_error = call_archagent_json(args, timeout_seconds=subprocess_timeout)

    if result_data is None:
        result_data = {
            "ok": False,
            "summary": json_error or "ArchAgent returned no valid JSON.",
        }

    session_id = result_data.get("session_id")
    session_link = None
    if isinstance(session_id, str) and SAFE_SESSION_ID_RE.fullmatch(session_id):
        session_link = url_for("session_view", session_id=session_id, project=str(project_path))

    return render_template(
        "run.html",
        project_path=str(project_path),
        project_display_path=relative_to_archagent_root(project_path),
        request_text=request_text,
        backend=backend,
        result=result_data,
        outcome=outcome,
        json_error=json_error,
        command_display=command_to_string(outcome.command),
        session_link=session_link,
    )


@app.get("/session/<session_id>")
def session_view(session_id: str) -> str:
    raw_project_path = request.args.get("project", "")

    try:
        project_path = resolve_project_path(raw_project_path)
    except ValueError as exc:
        flash(str(exc), "error")
        return redirect(url_for("index"))

    session_dir = session_dir_for(project_path, session_id)
    files = load_session_files(session_dir)
    result_data = parse_result_json(files.get("result.json"))
    patch_text = files.get("patch.diff", "")

    return render_template(
        "session.html",
        project_path=str(project_path),
        project_display_path=relative_to_archagent_root(project_path),
        session_id=session_id,
        session_dir=str(session_dir),
        files=files,
        file_order=SESSION_FILE_ORDER,
        result=result_data,
        patch_lines=diff_lines(patch_text),
    )


@app.get("/session-file/<session_id>/<filename>")
def session_file_view(session_id: str, filename: str) -> Response:
    """Return a recognised session file as text/plain for direct viewing/copying."""
    raw_project_path = request.args.get("project", "")

    try:
        project_path = resolve_project_path(raw_project_path)
    except ValueError:
        abort(404)

    safe_name = safe_session_filename(filename)
    session_dir = session_dir_for(project_path, session_id)
    target = (session_dir / safe_name).resolve()

    try:
        target.relative_to(session_dir)
    except ValueError:
        abort(404)

    if not target.exists() or not target.is_file():
        abort(404)

    text, _ = read_text_file_limited(target)
    return Response(text, mimetype="text/plain; charset=utf-8")


def _c2asm_render(mode: str = "cmini", **kwargs: Any) -> str:
    """Shared render call for all /c2asm routes."""
    return render_template(
        "c2asm.html",
        mode=mode,
        examples=EXAMPLE_CMINI_PROGRAMS,
        c_examples=EXAMPLE_C_PROGRAMS,
        gcc_available=bool(shutil.which("gcc")),
        **kwargs,
    )


@app.get("/c2asm")
def c2asm_page() -> str:
    return _c2asm_render(
        mode="cmini",
        prefill_source="",
        prefill_c_source="",
        result=None,
        crun_result=None,
        outcome=None,
        defaults={"timeout": 30, "assemble_bin": "", "emulate_bin": "", "c_timeout": 10},
    )


@app.post("/c2asm/run")
def c2asm_run() -> str:
    source = (request.form.get("source", "") or "").strip()
    assemble_bin = (request.form.get("assemble_bin", "") or "").strip()
    emulate_bin = (request.form.get("emulate_bin", "") or "").strip()
    emit_asm_only = request.form.get("emit_asm_only") == "on"
    timeout = clamp_int(request.form.get("timeout"), 30, minimum=5, maximum=120)

    if not source:
        flash("Source code cannot be empty.", "error")
        return redirect(url_for("c2asm_page"))

    if len(source) > 65536:
        flash("Source too large (max 64 KB).", "error")
        return redirect(url_for("c2asm_page"))

    args = ["--c2asm-code", source, "--json", "--timeout", str(timeout)]
    if assemble_bin:
        args.extend(["--assemble-bin", assemble_bin])
    if emulate_bin:
        args.extend(["--emulate-bin", emulate_bin])
    if emit_asm_only:
        args.append("--emit-asm-only")

    outcome = run_archagent(args, timeout_seconds=timeout + 15)
    result_data = extract_json_object(outcome.stdout)

    if result_data is None:
        result_data = {"ok": False, "error": outcome.stderr or "No JSON output.", "stage": "internal"}

    return _c2asm_render(
        mode="cmini",
        prefill_source=source,
        prefill_c_source="",
        result=result_data,
        crun_result=None,
        outcome=outcome,
        defaults={"timeout": timeout, "assemble_bin": assemble_bin, "emulate_bin": emulate_bin, "c_timeout": 10},
    )


@app.post("/c2asm/crun")
def c2asm_crun() -> str:
    """Compile and run a standard C program with gcc."""
    source = (request.form.get("source", "") or "").strip()
    timeout = clamp_int(request.form.get("timeout"), 10, minimum=2, maximum=60)

    if not source:
        flash("Source code cannot be empty.", "error")
        return redirect(url_for("c2asm_page"))

    if len(source) > 131072:
        flash("Source too large (max 128 KB).", "error")
        return redirect(url_for("c2asm_page"))

    if not shutil.which("gcc"):
        crun_result: Dict[str, Any] = {
            "ok": False, "stage": "compile",
            "error": "gcc not found on PATH. Install gcc to use the Standard C runner.",
        }
        return _c2asm_render(
            mode="c",
            prefill_source="", prefill_c_source=source,
            result=None, crun_result=crun_result, outcome=None,
            defaults={"timeout": 30, "assemble_bin": "", "emulate_bin": "", "c_timeout": timeout},
        )

    tmpdir: Optional[str] = None
    try:
        tmpdir = tempfile.mkdtemp(prefix="archagent_crun_")
        src_path = os.path.join(tmpdir, "prog.c")
        bin_path = os.path.join(tmpdir, "prog")

        with open(src_path, "w") as fh:
            fh.write(source)

        compile_proc = subprocess.run(
            [GCC_BIN, "-O1", "-std=c17", "-Wall", "-Wextra", "-o", bin_path, src_path],
            capture_output=True, text=True, timeout=30, cwd=tmpdir,
        )

        if compile_proc.returncode != 0:
            crun_result = {
                "ok": False,
                "stage": "compile",
                "compile_stderr": compile_proc.stderr,
                "compile_stdout": compile_proc.stdout,
                "returncode": compile_proc.returncode,
            }
        else:
            try:
                run_proc = subprocess.run(
                    [bin_path],
                    capture_output=True, text=True, timeout=timeout, cwd=tmpdir,
                )
                crun_result = {
                    "ok": run_proc.returncode == 0,
                    "stage": "run",
                    "stdout": run_proc.stdout,
                    "stderr": run_proc.stderr,
                    "returncode": run_proc.returncode,
                }
            except subprocess.TimeoutExpired:
                crun_result = {
                    "ok": False,
                    "stage": "run",
                    "error": f"Program timed out after {timeout} second(s).",
                    "timed_out": True,
                }

    except subprocess.TimeoutExpired:
        crun_result = {"ok": False, "stage": "compile", "error": "Compilation timed out."}
    except OSError as exc:
        crun_result = {"ok": False, "stage": "compile", "error": f"Could not run gcc: {exc}"}
    finally:
        if tmpdir:
            shutil.rmtree(tmpdir, ignore_errors=True)

    return _c2asm_render(
        mode="c",
        prefill_source="", prefill_c_source=source,
        result=None, crun_result=crun_result, outcome=None,
        defaults={"timeout": 30, "assemble_bin": "", "emulate_bin": "", "c_timeout": timeout},
    )


@app.errorhandler(404)
def not_found(_: Exception) -> Tuple[str, int]:
    return render_template(
        "error.html",
        title="Not found",
        message="The requested page, project, session, or audit file could not be found.",
    ), 404


@app.errorhandler(500)
def internal_error(_: Exception) -> Tuple[str, int]:
    return render_template(
        "error.html",
        title="Internal error",
        message="The GUI encountered an internal error while preparing the page.",
    ), 500


if __name__ == "__main__":
    app.run(host="127.0.0.1", port=5050, debug=False)
