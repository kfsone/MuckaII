// PreToolUse hook: blocks any tool call that names the reference client's tree or repo.
// MuckaII itself (G:\Source\MuckaII) must stay reachable, so "mucka" must not be followed by
// a letter or digit.
import { readFileSync } from "node:fs";

// Matched against the raw hook payload, so JSON-escaped backslashes and a payload that fails to
// parse are both covered.
const REFERENCE = [
  /[\\/]+source[\\/]+mucka(?![a-z0-9])/i,
  /kfsone[\\/]+mucka(?![a-z0-9])/i,
];

const text = readFileSync(0, "utf8");
if (REFERENCE.some((re) => re.test(text))) {
  process.stderr.write(
    "Blocked by the reference firewall: G:\\Source\\mucka and kfsone/mucka are closed to " +
      "MuckaII work. The reference reaches this project only through docs/spec/. If the spec " +
      "is silent, ask the director.\n",
  );
  process.exit(2);
}
process.exit(0);
