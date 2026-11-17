import { readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import wabtFactory from "wabt";
import { root, generated, sha256 } from "./lib/modding.mjs";
import { mergeWorkspace } from "./lib/workspace.mjs";
import { externalFunctionABI, assertFunctionABI } from "./lib/wat.mjs";

const [wat, original, symbols] = await Promise.all([
  readFile(path.join(generated, "index.wat"), "utf8"),
  readFile(path.join(root, "webapp/index.wasm")),
  readFile(path.join(generated, "symbols.json"), "utf8").then(JSON.parse),
]);
if (sha256(original) !== symbols.wasmSha256) {
  throw new Error(
    "Original WASM changed since decompilation; regenerate before building.",
  );
}
const wabt = await wabtFactory();
const source = process.argv.includes("--monolithic")
  ? { wat, changed: [], workspace: false }
  : await mergeWorkspace(
      path.join(generated, "workspace"),
      wat,
      sha256(original),
    );
// JS reflection only exposes import/export names and kinds, not signatures.
// Recover the baseline directly from the actual original, not editable metadata.
const originalModule = wabt.readWasm(original, {});
let originalABI;
try {
  originalModule.generateNames();
  originalModule.applyNames();
  originalABI = externalFunctionABI(
    originalModule.toText({ foldExprs: false, inlineExport: false }),
  );
} finally {
  originalModule.destroy();
}
assertFunctionABI(originalABI, externalFunctionABI(source.wat));
const module = wabt.parseWat("index.wat", source.wat);
try {
  module.resolveNames();
  module.validate();
  const { buffer } = module.toBinary({ write_debug_names: true });
  const compiled = new WebAssembly.Module(buffer);
  const baseline = new WebAssembly.Module(original);
  for (const kind of ["imports", "exports"]) {
    if (
      JSON.stringify(WebAssembly.Module[kind](compiled)) !==
      JSON.stringify(WebAssembly.Module[kind](baseline))
    ) {
      throw new Error(
        `WASM ${kind} changed: keep the loader ABI names, kinds and order intact.`,
      );
    }
  }
  const output = path.join(root, "webapp/index.modded.wasm");
  await writeFile(output, buffer);
  console.log(
    source.workspace
      ? `Function workspace: ${source.changed.length} edited functions${source.changed.length ? ` (${source.changed.join(", ")})` : ""}.`
      : "Source: monolithic index.wat.",
  );
  console.log(
    `Built ${path.relative(root, output)}. Run npm run native:build to compile it into the native game.`,
  );
  console.log(
    "Original index.wasm is untouched. Interface names/kinds and function signatures checked; test behavior in the native build.",
  );
} finally {
  module.destroy();
}
