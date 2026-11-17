// Inspect initialized WASM data without running constructors or gameplay.
import { readFile, readdir } from "node:fs/promises";
import path from "node:path";
import { root, generated, sha256, writeJSON } from "./lib/modding.mjs";

const wasm = await readFile(path.join(root, "webapp/index.wasm"));
const module = new WebAssembly.Module(wasm);
const imports = {};
for (const item of WebAssembly.Module.imports(module)) {
  if (item.kind !== "function")
    throw new Error("Unexpected non-function import");
  (imports[item.module] ??= {})[item.name] = () => 0;
}
const instance = new WebAssembly.Instance(module, imports);
const memory = Object.values(instance.exports).find(
  (value) => value instanceof WebAssembly.Memory,
);
const bytes = Buffer.from(memory.buffer);
const directory = path.join(generated, "workspace/pseudocode");
const functions = await Promise.all(
  (await readdir(directory)).map(async (file) => ({
    file,
    text: await readFile(path.join(directory, file), "utf8"),
  })),
);
const queries = process.argv.slice(2);
const strings = queries.length
  ? queries
  : [
      "Set Gravity got invalid",
      "Set Mass/Friction/Bounciness",
      "Linear/Angular Limits",
      "Add Force got invalid",
      "Restart!",
      "DebugPhysics",
      "Custom Physics",
      "After Physics",
      "restarts",
    ];
const report = strings.map((query) => {
  const address = bytes.indexOf(Buffer.from(query));
  return {
    query,
    address,
    references:
      address < 0
        ? []
        : functions
            .filter(({ text }) => new RegExp(`\\b${address}\\b`).test(text))
            .map(({ file }) => file),
  };
});
await writeJSON(path.join(generated, "gameplay-research.json"), {
  wasmSha256: sha256(wasm),
  strings: report,
});
console.log(JSON.stringify(report, null, 2));
