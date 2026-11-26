"""Check local Markdown links and boot examples from the BASIC comparison."""
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
errors = []
for path in root.rglob("*.md"):
    if any(part in {".git", "target"} for part in path.relative_to(root).parts):
        continue
    for match in re.finditer(r"\]\(([^)]+)\)", path.read_text()):
        target = match.group(1).split("#", 1)[0]
        if target and not re.match(r"[\w+.-]+:|/", target):
            if not (path.parent / target).exists():
                errors.append(f"{path.relative_to(root)}: missing {target}")
if errors:
    sys.exit("\n".join(errors))
print("Local Markdown file links: OK")
if len(sys.argv) > 1:
    binary = Path(sys.argv[1]).resolve()
    source = (root / "docs/basic-comparison.md").read_text()
    examples = re.search(r"```basic\n(.*?)\n```", source, re.S).group(1).splitlines()
    for model in ("kestrel", "kestrel16"):
        args = [str(binary), "--model", model]
        for line in examples:
            args.extend(["--type", line])
        result = subprocess.run(args, cwd=root, check=True, capture_output=True, text=True, timeout=60)
        rows = {row.strip() for row in result.stdout.splitlines()}
        assert {"4", "HELLO", "30001", "20000"} <= rows, result.stdout
        print(f"{model}: documented BASIC examples OK")
