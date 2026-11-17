import { mkdir, writeFile, access } from "node:fs/promises";
import path from "node:path";
import { spawnSync } from "node:child_process";
import wabtFactory from "wabt";
import prettier from "prettier";
import { stripLocalDebugNames } from "./lib/wasm.mjs";
import {
  root,
  generated,
  sha256,
  loadInputs,
  recoverSymbols,
  writeJSON,
} from "./lib/modding.mjs";

const force = process.argv.includes("--force");
const watPath = path.join(generated, "index.wat");
try {
  await access(watPath);
  if (!force) {
    throw new Error(
      "Decompilation already exists; refusing to overwrite your WAT edits. " +
        "Copy your changes elsewhere, then use npm run decompile -- --force.",
    );
  }
} catch (error) {
  if (error.code !== "ENOENT") throw error;
}

const tool =
  process.env.WASM_DECOMPILE ||
  path.join(
    generated,
    "tools/wabt-1.0.39/bin",
    process.platform === "win32" ? "wasm-decompile.exe" : "wasm-decompile",
  );
try {
  await access(tool);
} catch {
  throw new Error(
    "Native wasm-decompile not found. Run npm run tools:install, " +
      "or set WASM_DECOMPILE to an absolute executable path.",
  );
}

const { loader, wasm, data, metadata } = await loadInputs();
const symbols = recoverSymbols(loader);
const nativeModule = new WebAssembly.Module(wasm);
const wabt = await wabtFactory();
const module = wabt.readWasm(wasm, { readDebugNames: true });
let wat;
try {
  module.generateNames();
  module.applyNames();
  wat = module.toText({ foldExprs: false, inlineExport: false });
} finally {
  module.destroy();
}

// Only rename WAT identifiers, never strings/data or the actual ABI names.
const renames = new Map();
for (const match of wat.matchAll(
  /\(import "([^"]+)" "([^"]+)" \(func (\$[^\s()]+)/g,
)) {
  const name = symbols.imports[match[2]];
  if (name) renames.set(match[3], `$import_${name}`);
}
for (const match of wat.matchAll(
  /\(export "([^"]+)" \(func (\$[^\s()]+)\)\)/g,
)) {
  const name = symbols.exports[match[1]];
  if (name) renames.set(match[2], `$${name}`);
}
wat = wat.replace(/"(?:[^"\\]|\\.)*"|\$[^\s()]+/g, (token) =>
  token.startsWith("$") ? (renames.get(token) ?? token) : token,
);

await mkdir(generated, { recursive: true });
await writeFile(watPath, wat);
const namedModule = wabt.parseWat("index.wat", wat);
try {
  namedModule.resolveNames();
  namedModule.validate();
  await writeFile(
    path.join(generated, "index.named.wasm"),
    stripLocalDebugNames(
      namedModule.toBinary({ write_debug_names: true }).buffer,
    ),
  );
} finally {
  namedModule.destroy();
}

const result = spawnSync(
  tool,
  [
    path.join(generated, "index.named.wasm"),
    "-o",
    path.join(generated, "index.dcmp"),
  ],
  { encoding: "utf8", maxBuffer: 4 * 1024 * 1024 },
);
if (result.error) throw result.error;
if (result.status !== 0)
  throw new Error(
    "wasm-decompile failed. Run npm run tools:install for native WABT " +
      "(the npm build overflows on this module), then npm run decompile -- --force.\n" +
      result.stderr,
  );

await writeFile(
  path.join(generated, "loader.pretty.js"),
  await prettier.format(loader, { parser: "babel" }),
);
const strings = [];
for (const match of wasm.toString("latin1").matchAll(/[\x20-\x7e]{6,}/g)) {
  strings.push({ fileOffset: match.index, text: match[0] });
}
await writeJSON(path.join(generated, "strings.json"), strings);
await writeJSON(path.join(generated, "symbols.json"), {
  wasmSha256: sha256(wasm),
  imports: WebAssembly.Module.imports(nativeModule).map((item) => ({
    ...item,
    recoveredName: symbols.imports[item.name] ?? null,
  })),
  exports: WebAssembly.Module.exports(nativeModule).map((item) => ({
    ...item,
    recoveredName: symbols.exports[item.name] ?? null,
  })),
  assets: metadata.files.length,
  dataSha256: sha256(data),
});
console.log(`Decompiled to ${path.relative(root, generated)}/`);
console.log(
  `Recovered ${Object.keys(symbols.exports).length} export names and ${Object.keys(symbols.imports).length} import names.`,
);
console.log(
  "Edit index.wat to rebuild; index.dcmp is read-only C-like pseudocode, not C source.",
);
