---
name: Bug report
about: Report incorrect or unexpected behaviour
labels: bug
---

## Description

A clear description of the bug.

## Steps to reproduce

```sh
# Paste the exact command that triggered the bug
./bin/archagent --project ... --request "..." --backend mock --json
```

## Expected behaviour

What you expected to happen.

## Actual behaviour

What happened instead. Paste any relevant output:

```
<paste output here>
```

## Session artefacts

If applicable, paste the contents of the session directory:

```
.archagent/sessions/<id>/
├── prompt.txt
├── model_response.txt
├── patch.diff
├── validation_report.txt
└── events.jsonl
```

## Environment

- OS: (e.g. Ubuntu 24.04, macOS 15.2)
- Compiler: (e.g. gcc 13.2, clang 18)
- Backend: (mock / llama / ollama)
- Model (if applicable): (e.g. qwen2.5-coder-7b-instruct-q4_k_m.gguf)

## ArchAgent version / commit

```sh
git log -1 --oneline
```
