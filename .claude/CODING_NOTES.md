# Coding Best Practices & Reminders

> **Style rule:** Notes must be clear and concise — 300 characters or less each. Group by topic, not by date. Whenever a PR review (CodeRabbit or human) catches a mistake, add or amend a note here right away so it isn't repeated.

## Resource Cleanup & Temporary Files

**IMPORTANT**: Always add proper cleanup code in programs to prevent lingering temp files after closing.

### Best Practices:

1. **GUI Applications (PyQt, Tkinter, etc.)**
   - Implement `closeEvent()` handler to cleanup resources on window close
   - Call `deleteLater()` on widgets to ensure proper Qt object cleanup
   - Process pending events with `app.processEvents()` before exit

2. **File Handling**
   - Use context managers (`with` statements) for file operations
   - Explicitly close file handles when not using context managers
   - Release file locks before program exit
   - Clean up temporary files in temp directories

3. **Background Threads & Workers**
   - Stop and join all background threads before exit
   - Cancel any pending operations
   - Clean up thread-specific resources

4. **Testing Cleanup**
   - After closing the program, verify the executable can be:
     - Deleted immediately
     - Moved to another location
     - Replaced with a new version
   - If the file is locked, cleanup code is missing or incomplete

### Example Implementation (PyQt6):

```python
def closeEvent(self, event):
    """Handle window close event - ensure proper cleanup"""
    # Cleanup modules/components
    for module in self.modules:
        try:
            module.cleanup()
        except Exception as e:
            print(f"Error cleaning up module: {e}")

    # Save state
    self.save_settings()

    # Accept close event
    event.accept()
    QApplication.quit()

def main():
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()

    exit_code = app.exec()

    # Final cleanup
    window.deleteLater()
    app.processEvents()

    sys.exit(exit_code)
```

### PyInstaller Specific:

In `.spec` file, add:
```python
exe = EXE(
    ...
    bootloader_ignore_signals=True,  # Better cleanup handling
    ...
)
```

## Date: 2025-12-16
This note was created based on issues encountered with PyInstaller executables remaining locked after closing.

## Branch Protection

- **GitHub branch protection requires a public repo, or GitHub Pro/Team/Enterprise, on private repos** — `PUT .../branches/.../protection` 403s with "Upgrade to GitHub Pro or make this repository public" otherwise. The bootstrap workflow handles this by warning and continuing, not failing the job — check the workflow run's log if a downstream repo's protection didn't apply.
- **Don't set required status-check contexts before the repo has CI that produces them.** A required context that never reports a status permanently blocks merges. `ci.yml` isn't part of the bootstrap file set (it needs per-project tailoring — see "tailor or language-agnostic" above), so bootstrap-time protection only sets what's safe without knowing future job names (no force-push, no deletion, enforced for admins); add required checks once that repo's own CI is customized and green.
- **Avoid `required_pull_request_reviews` on a solo-maintained repo.** GitHub won't let an author approve their own PR, so requiring even 1 approval with no other reviewer deadlocks every merge. Rely on required status checks (which do block direct pushes too, since a bare push's commit never gets the PR-triggered check runs) instead of an approval-count gate.

## C++ Input Validation & Output Formats

- **Validate public-API inputs.** Reject negative penalties and distances, non-finite coordinates and out-of-range cells by throwing or returning false; never assume callers pass sane values.
- **Bounds-check both ends before walking a segment.** A helper that takes two cells (e.g. `lineOfSight`) must return false for out-of-grid endpoints, not index outside the grid.
- **Format numbers for file formats with a classic-locale stream, not printf.** A comma-decimal global locale makes `%f` write `46,100000`, which is invalid GPX/XML.
- **Strip XML 1.0-forbidden control characters from text.** Everything below 0x20 except tab, LF and CR is illegal even when escaped; escaping alone does not make it valid.

## General Style Notes

- **Keep lines under 120 characters.** Long lines are hard to review side-by-side in a diff or split editor pane, and tend to signal a line doing too many things at once. Wrap or break up expressions rather than letting them run long.
- **Add docstrings to explain code.** Focus on *why* a function/class exists or *why* it does something non-obvious — the code itself already shows *what* it does. A docstring worth writing usually covers intent, assumptions, edge cases, or a gotcha a future reader would otherwise have to rediscover the hard way.
- **Strip docstrings when building a release.** Release builds don't need internal rationale shipped alongside the binary — it bloats the artifact and can leak implementation notes you didn't mean to publish. Run Python with `-OO` (or an equivalent build step) to drop docstrings and assertions from the compiled output before packaging.
