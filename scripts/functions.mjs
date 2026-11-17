import { readFile } from "node:fs/promises";
import path from "node:path";
import { generated, root, sha256 } from "./lib/modding.mjs";
import { splitWorkspace } from "./lib/workspace.mjs";
import { formatSignature } from "./lib/wat.mjs";

const directory = path.join(generated, "workspace");
const command = process.argv[2];
if (command === "split") {
  const [wat, wasm, symbols] = await Promise.all([
    readFile(path.join(generated, "index.wat"), "utf8"),
    readFile(path.join(root, "webapp/index.wasm")),
    readFile(path.join(generated, "symbols.json"), "utf8").then(JSON.parse),
  ]);
  if (sha256(wasm) !== symbols.wasmSha256)
    throw new Error("Original WASM changed; regenerate analysis first");
  let pseudocode;
  try {
    pseudocode = await readFile(path.join(generated, "index.dcmp"), "utf8");
  } catch (error) {
    if (error.code !== "ENOENT") throw error;
  }
  const manifest = await splitWorkspace(directory, wat, {
    wasmSha256: sha256(wasm),
    pseudocode,
  });
  console.log(
    `Split ${manifest.functions.filter((item) => !item.imported).length} editable functions into modding/generated/workspace/functions/`,
  );
  console.log(
    "Browse workspace/REPORT.md or run npm run wasm:inspect -- get_app_version.",
  );
} else if (command === "inspect") {
  let manifest;
  try {
    manifest = JSON.parse(
      await readFile(path.join(directory, "index.json"), "utf8"),
    );
  } catch (error) {
    if (error.code === "ENOENT")
      throw new Error("Run npm run wasm:split first.");
    throw error;
  }
  const query = process.argv[3];
  if (!query || query.startsWith("--"))
    throw new Error(
      "Usage: npm run wasm:inspect -- <name-or-#index> [--wat | --pseudocode | --json]",
    );
  const exact = manifest.functions.find(
    (item) =>
      item.id === query ||
      item.id.slice(1) === query ||
      `#${item.index}` === query,
  );
  if (exact) {
    if (process.argv.includes("--json"))
      console.log(JSON.stringify(exact, null, 2));
    else if (
      process.argv.includes("--wat") ||
      process.argv.includes("--pseudocode")
    ) {
      const file = process.argv.includes("--wat")
        ? exact.file
        : exact.pseudocodeFile;
      if (!file)
        throw new Error(
          "No requested source file for this function (it may be a JS import).",
        );
      process.stdout.write(await readFile(path.join(directory, file), "utf8"));
    } else {
      console.log(
        `${exact.id}  #${exact.index}  ${formatSignature(exact.signature)}`,
      );
      console.log(`Location: index.wat:${exact.line}-${exact.endLine}`);
      console.log(`Export names: ${exact.exports.join(", ") || "(internal)"}`);
      if (exact.imported)
        console.log(`JS import: ${exact.importModule}.${exact.importName}`);
      if (exact.file)
        console.log(`Editable: modding/generated/workspace/${exact.file}`);
      if (exact.pseudocodeFile)
        console.log(
          `Pseudocode: modding/generated/workspace/${exact.pseudocodeFile} (analysis snapshot)`,
        );
      console.log(`Calls: ${exact.calls.join(", ") || "(none)"}`);
      console.log(
        `Called by: ${exact.callers.join(", ") || "(no direct callers)"}`,
      );
      console.log(
        `Indirect call sites: ${exact.indirectCalls} (targets unresolved)`,
      );
      console.log(
        "Signatures and call references describe the split snapshot, not subsequent edits.",
      );
    }
  } else {
    if (
      process.argv.some((arg) =>
        ["--wat", "--pseudocode", "--json"].includes(arg),
      )
    ) {
      throw new Error(
        "Source/JSON output requires an exact function name or #index.",
      );
    }
    const matches = manifest.functions.filter((item) =>
      [item.id, ...item.exports, item.importName ?? ""].some((name) =>
        name.toLowerCase().includes(query.toLowerCase()),
      ),
    );
    for (const item of matches.slice(0, 100))
      console.log(
        `#${item.index}  ${item.id}  ${formatSignature(item.signature)}`,
      );
    console.log(
      `${matches.length} matches${matches.length > 100 ? " (showing first 100)" : ""}. Use an exact name to inspect callers and source paths.`,
    );
    if (!matches.length) process.exitCode = 1;
  }
} else
  throw new Error("Usage: node scripts/functions.mjs split | inspect <query>");
