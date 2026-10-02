// PreToolUse hook (all tools): keeps the previous client (Mucka) out of MuckaII work.
// Matching is against the raw hook payload, so JSON-escaped backslashes and a payload that
// fails to parse are both covered. Exit 2 blocks the call.
import { readFileSync } from "node:fs";

const raw = readFileSync(0, "utf8");
let toolName = "";
try {
  toolName = JSON.parse(raw).tool_name ?? "";
} catch {
  // Unparseable payloads are still matched below.
}

const SEP = String.raw`[\\/]+`;

const RULES = [
  {
    // Any path component named "mucka": G:\Source\mucka, ../../mucka, C:\Program Files\Mucka,
    // kfsone/mucka. MuckaII and ~/.mucka (the wire corpus) do not match; ~/mucka.ini (user data
    // for the importer) is allowed.
    re: new RegExp(SEP + String.raw`mucka(?![a-z0-9_]|\.ini)`, "i"),
    why: "names the previous client's tree, install or repo",
  },
  {
    // The bare G:\Source directory: recursive searches and cd from there reach the reference.
    re: new RegExp(String.raw`(?:g:|[\\/]g)` + SEP + String.raw`source(?![\\/]*[a-z0-9_.-])`, "i"),
    why: "targets G:\\Source itself; name the sibling you need (MuckaII, mud-fe, clio-1.8a)",
  },
  {
    // Wildcards directly under G:\Source (G:\Source\mu*, /g/Source/*).
    re: new RegExp(String.raw`(?:g:|[\\/]g)` + SEP + String.raw`source` + SEP + String.raw`[^\\/\s"']*[*?\[]`, "i"),
    why: "uses a wildcard directly under G:\\Source",
  },
  {
    // The tripwire list holds the reference's vocabulary; only the check script reads it.
    re: /reference-identifiers/i,
    why: "reads the tripwire list, which is the reference's vocabulary",
  },
];

// The firewall's own files: never written by an agent, through any tool.
const SELF = /\.claude[\\/]+(?:settings[^\\/"]*\.json|hooks)/i;
const WRITERS = new Set(["Edit", "Write", "MultiEdit", "NotebookEdit", "Bash", "PowerShell"]);

function block(why) {
  process.stderr.write(
    "Blocked by the reference firewall: the call " + why + ". The previous client reaches " +
      "MuckaII only through docs/spec/. If the spec is silent, ask the director. The firewall " +
      "files are edited by the director by hand.\n",
  );
  process.exit(2);
}

for (const rule of RULES) {
  if (rule.re.test(raw)) block(rule.why);
}
if (WRITERS.has(toolName) && SELF.test(raw)) block("would change the firewall's own files");
process.exit(0);
