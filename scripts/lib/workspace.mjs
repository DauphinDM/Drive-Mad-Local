import { mkdir, readFile, writeFile, access } from "node:fs/promises";
import path from "node:path";
import { sha256, writeJSON } from "./modding.mjs";
import {
  analyzeWat,
  moduleForms,
  formatSignature,
  pseudocodeFunctions,
  tokens,
} from "./wat.mjs";

function fileStem(item) {
  return `${String(item.index).padStart(5, "0")}-${item.id.slice(1).replace(/[^A-Za-z0-9_-]/g, "_")}`;
}

export async function splitWorkspace(
  directory,
  wat,
  { wasmSha256, pseudocode } = {},
) {
  try {
    await access(directory);
    throw new Error(
      "Function workspace already exists. Move it elsewhere to protect edits before splitting again.",
    );
  } catch (error) {
    if (error.code !== "ENOENT") throw error;
  }
  const functions = analyzeWat(wat);
  const decompiled = pseudocode ? pseudocodeFunctions(pseudocode) : new Map();
  const manifest = {
    version: 1,
    sourceSha256: sha256(wat),
    wasmSha256,
    functions,
  };
  await mkdir(path.join(directory, "functions"), { recursive: true });
  await mkdir(path.join(directory, "pseudocode"), { recursive: true });
  // Bounded batches avoid exhausting file descriptors on the full game.
  for (let offset = 0; offset < functions.length; offset += 32) {
    await Promise.all(
      functions.slice(offset, offset + 32).map(async (item) => {
        if (item.imported) return;
        const body = wat.slice(item.start, item.end) + "\n";
        item.file = `functions/${fileStem(item)}.wat`;
        item.sha256 = sha256(body);
        await writeFile(path.join(directory, item.file), body);
        const code = decompiled.get(item.index);
        if (code) {
          item.pseudocodeFile = `pseudocode/${fileStem(item)}.dcmp`;
          await writeFile(path.join(directory, item.pseudocodeFile), code);
        }
      }),
    );
  }
  await writeJSON(path.join(directory, "index.json"), manifest);
  const link = (item) =>
    item.file ? `[${item.id}](${item.file})` : `\`${item.id}\` (JS import)`;
  const table = (items) =>
    [
      "| Index | Function | WASM signature | Direct callers | Direct callees |",
      "| --- | --- | --- | ---: | ---: |",
      ...items.map(
        (item) =>
          `| ${item.index} | ${link(item)} | \`${formatSignature(item.signature)}\` | ${item.callers.length} | ${item.calls.length} |`,
      ),
    ].join("\n");
  await writeFile(
    path.join(directory, "REPORT.md"),
    [
      "# Function workspace",
      "",
      `${functions.length} functions: ${functions.filter((item) => item.imported).length} JS imports, ${functions.filter((item) => !item.imported).length} WASM definitions.`,
      "",
      "Edit `functions/*.wat`, then run `npm run wasm:build` from the project root.",
      "Do not edit index.json. Keep the parent index.wat unchanged while using this workspace.",
      "Pseudocode files are analysis-only snapshots and do not get recompiled.",
      "",
      "**Call references are static direct calls only.** Indirect calls/function pointers are not resolved.",
      "Types are WASM machine types, not recovered C++ types or proof that a call is safe.",
      "",
      "## Exported entry points",
      "",
      table(functions.filter((item) => item.exports.length)),
      "",
      "## Most directly referenced internal functions",
      "",
      table(
        functions
          .filter((item) => !item.imported && !item.exports.length)
          .sort((a, b) => b.callers.length - a.callers.length)
          .slice(0, 30),
      ),
      "",
      "## Full index",
      "",
      table(functions),
      "",
    ].join("\n"),
  );
  return manifest;
}

export async function mergeWorkspace(directory, wat, wasmSha256) {
  let manifest;
  try {
    manifest = JSON.parse(
      await readFile(path.join(directory, "index.json"), "utf8"),
    );
  } catch (error) {
    if (error.code === "ENOENT") {
      // A half-created workspace must not silently fall back to another source.
      try {
        await access(directory);
      } catch (missing) {
        if (missing.code === "ENOENT")
          return { wat, changed: [], workspace: false };
        throw missing;
      }
      throw new Error(
        "Function workspace has no index.json; move the incomplete workspace before building.",
      );
    }
    throw error;
  }
  if (manifest.version !== 1 || !Array.isArray(manifest.functions))
    throw new Error("Invalid function workspace manifest");
  if (
    manifest.sourceSha256 !== sha256(wat) ||
    manifest.wasmSha256 !== wasmSha256
  ) {
    throw new Error(
      "Function workspace is stale or index.wat was edited too. Move the workspace before re-splitting, " +
        "or use npm run wasm:build -- --monolithic to explicitly ignore function files.",
    );
  }
  const definitions = analyzeWat(wat).filter((item) => !item.imported);
  const entries = manifest.functions.filter((item) => !item.imported);
  if (definitions.length !== entries.length)
    throw new Error("Workspace function list changed");
  const parts = [];
  const changed = [];
  let end = 0;
  for (let offset = 0; offset < definitions.length; offset++) {
    const item = definitions[offset];
    const entry = entries[offset];
    const file = `functions/${fileStem(item)}.wat`;
    const original = wat.slice(item.start, item.end) + "\n";
    if (
      entry.id !== item.id ||
      entry.index !== item.index ||
      entry.file !== file ||
      entry.start !== item.start ||
      entry.end !== item.end ||
      entry.sha256 !== sha256(original)
    ) {
      throw new Error(
        `Workspace metadata changed for ${item.id}; do not edit index.json`,
      );
    }
    const edited = await readFile(path.join(directory, file), "utf8");
    parts.push(wat.slice(end, item.start));
    if (sha256(edited) === entry.sha256)
      parts.push(wat.slice(item.start, item.end));
    else {
      const wrapped = `(module\n${edited}\n)`;
      const forms = moduleForms(wrapped);
      const first =
        forms.length === 1 && forms[0].head === "func"
          ? [...takeTokens(wrapped.slice(forms[0].start, forms[0].end), 3)]
          : [];
      if (first[2]?.value !== item.id) {
        throw new Error(
          `Expected exactly one unchanged function identifier ${item.id} in ${file}`,
        );
      }
      changed.push(item.id);
      parts.push(edited.trimEnd());
    }
    end = item.end;
  }
  parts.push(wat.slice(end));
  return { wat: parts.join(""), changed, workspace: true };
}

function* takeTokens(text, count) {
  let seen = 0;
  for (const token of tokens(text)) {
    yield token;
    if (++seen === count) break;
  }
}
