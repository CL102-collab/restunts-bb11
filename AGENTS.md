If you are fixing a bug, make sure the bug is generic as much as possible and not just putting a band-aid to the failing test case.

Break down work to smaller tasks and resolve them step-by-step.

If something is not clear or ambiguous, ask questions before starting to work.

Check .gitattributes to see what kind of line endings to use in the file. If there is no rule for it, then use the same line endings what are already in the file (if it is mixed, use that what have the most).

Before staging or commit, ensure that executable bits are not set for files which shouldn't have this bit set.

Quit DOSBox by SIGKILL, otherwise it is popping up a confirmation window. Set `core` to `dynamic` and `cycles` to `max`, for maximum performance.

Check the project root's readme.md for code formatting standards and check if the new code meets them, use the needed tools to enforce the rules.

Do not write magic values, instead: use defined constants.

Do not use native types (int, char, etc.). Use the project's platform independent types (legacy_*).
