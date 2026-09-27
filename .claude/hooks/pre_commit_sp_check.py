"""
Pre-commit coding-notes pattern checker for Claude Code (Windows-compatible).
Triggered via PreToolUse hook on Bash tool calls.
Reads tool input JSON from stdin, skips non-commit commands,
then checks the staged diff against known .claude/CODING_NOTES.md anti-patterns.
"""

import sys
import json
import re
import shlex
import shutil
import subprocess


def _is_git_commit_command(command: str) -> bool:
    """Return True only when command invokes the git commit subcommand."""
    try:
        tokens = shlex.split(command, posix=True)
    except ValueError:
        return bool(re.search(r"git\s+commit", command))

    try:
        git_idx = next(i for i, t in enumerate(tokens) if t == "git" or t.endswith("/git"))
    except StopIteration:
        return False

    # Options that consume the following token as an argument
    _ARG_OPTIONS = frozenset({
        "-C", "-c", "--exec-path", "--git-dir",
        "--work-tree", "--namespace", "--super-prefix",
        "--list-cmds",
    })
    i = git_idx + 1
    while i < len(tokens):
        tok = tokens[i]
        if tok in _ARG_OPTIONS:
            i += 2
        elif tok.startswith("-"):
            i += 1
        else:
            return tok == "commit"
    return False


def get_staged_diff() -> str:
    git = shutil.which("git")
    if not git:
        return ""
    try:
        result = subprocess.run(
            [git, "diff", "--cached"],
            capture_output=True, text=True
        )
        return result.stdout
    except (OSError, subprocess.SubprocessError):
        return ""


def main():
    try:
        data = json.load(sys.stdin)
        # A PreToolUse hook matched on Bash receives the command nested at tool_input.command, not
        # top-level — reading the top-level key always returned "", so this check silently no-op'd
        # on every single commit since it was written. Confirmed against Claude Code's real
        # PreToolUse hook input schema and caught by CodeRabbit on i-machine-arr/reaparr PR #2.
        command = data.get("tool_input", {}).get("command", "")
    except json.JSONDecodeError:
        sys.exit(0)

    if not _is_git_commit_command(command):
        sys.exit(0)

    diff = get_staged_diff()
    if not diff:
        sys.exit(0)

    added_lines = [line for line in diff.splitlines() if line.startswith("+") and not line.startswith("+++")]

    warnings = []

    # ---------------------------------------------------------------
    # Coding-notes checks — add new entries here as CodeRabbit reviews land
    # ---------------------------------------------------------------

    # Broad except Exception
    for line in added_lines:
        if re.search(r"except\s+Exception\b", line):
            warnings.append(
                "Broad 'except Exception' detected — use specific exceptions "
                "(e.g. OSError, AttributeError)."
            )
            break

    # ---------------------------------------------------------------

    if warnings:
        print()
        print(f"Coding Notes Pre-commit Check — {len(warnings)} issue(s) found:")
        for w in warnings:
            print(f"  * {w}")
        print()
        print("Review .claude/CODING_NOTES.md before proceeding. Commit is NOT blocked — "
              "fix on next commit if intentional.")
        print()

    sys.exit(0)


if __name__ == "__main__":
    main()
