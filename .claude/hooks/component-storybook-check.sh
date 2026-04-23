#!/bin/bash
# component-storybook-check.sh
# PostToolUse hook: reminds to add storybook entries for new/modified components.
# Reads hook input JSON from stdin, checks if file is in core/*/components/,
# and verifies a matching storybook entry exists.

set -e

# Read JSON input from stdin
INPUT=$(cat)

# Extract file_path from the tool input
FILE_PATH=$(echo "$INPUT" | grep -o '"file_path"[[:space:]]*:[[:space:]]*"[^"]*"' | head -1 | sed 's/.*"file_path"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/')

# If no file_path found, exit silently
if [ -z "$FILE_PATH" ]; then
    exit 0
fi

# Check if the file is in core/src/components/ or core/include/components/
case "$FILE_PATH" in
    */core/src/components/*|*/core/include/components/*)
        ;;
    *)
        exit 0
        ;;
esac

# Extract component name from filename (e.g., cui_toggle.h -> toggle)
BASENAME=$(basename "$FILE_PATH" | sed 's/^cui_//' | sed 's/\.[ch]$//')

# Skip non-component files
if [ -z "$BASENAME" ]; then
    exit 0
fi

# Check if storybook.c has a matching include
STORYBOOK_FILE="$(dirname "$FILE_PATH" | sed 's|/core/.*||')/storybook/storybook.c"

if [ ! -f "$STORYBOOK_FILE" ]; then
    # Try relative to known project root
    STORYBOOK_FILE="/Users/r11/Projects/cui/storybook/storybook.c"
fi

if [ ! -f "$STORYBOOK_FILE" ]; then
    exit 0
fi

if grep -q "cui_${BASENAME}\.h" "$STORYBOOK_FILE" 2>/dev/null; then
    # Already has a storybook entry
    exit 0
fi

# No storybook entry found — output reminder as additionalContext
cat <<EOF
{"additionalContext": "Component '${BASENAME}' was modified but has no storybook entry. Consider using the storybook-agent to add a demo for cui_${BASENAME} in storybook/storybook.c."}
EOF

exit 0
