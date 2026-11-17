import { mkdir, writeFile, access } from "node:fs/promises";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { generated, sha256 } from "./lib/modding.mjs";

// The npm wasm-decompile build has a small fixed WASM stack and overflows on
// this game. Use the upstream native build for the C-like pseudocode pass.
// Later WABT releases dropped wasm-decompile; keep this version pinned.
const version = "1.0.39";
const platforms = {
  "linux-x64": "linux-x64",
  "linux-arm64": "linux-arm64",
  "darwin-arm64": "macos-arm64",
  "win32-x64": "windows-x64",
};
const platform = platforms[`${process.platform}-${process.arch}`];
if (!platform) {
  throw new Error(
    "Install native WABT yourself and set WASM_DECOMPILE to its wasm-decompile executable.",
  );
}
const directory = path.join(generated, "tools");
const binary = path.join(
  directory,
  `wabt-${version}`,
  "bin",
  process.platform === "win32" ? "wasm-decompile.exe" : "wasm-decompile",
);
try {
  await access(binary);
  console.log(`WABT already installed: ${binary}`);
  process.exit(0);
} catch {}

const filename = `wabt-${version}-${platform}.tar.gz`;
const url = `https://github.com/WebAssembly/wabt/releases/download/${version}/${filename}`;
console.log(`Downloading native WABT ${version} (${platform}) from GitHub...`);
const [archiveResponse, checksumResponse] = await Promise.all([
  fetch(url),
  fetch(`${url}.sha256`),
]);
if (!archiveResponse.ok || !checksumResponse.ok)
  throw new Error("WABT download failed");
const archive = Buffer.from(await archiveResponse.arrayBuffer());
const expected = (await checksumResponse.text()).match(
  /\b[0-9a-f]{64}\b/i,
)?.[0];
if (!expected || sha256(archive) !== expected.toLowerCase())
  throw new Error("WABT checksum verification failed");
await mkdir(directory, { recursive: true });
const archivePath = path.join(directory, filename);
await writeFile(archivePath, archive);
const result = spawnSync("tar", ["-xzf", archivePath, "-C", directory], {
  encoding: "utf8",
});
if (result.error) throw result.error;
if (result.status !== 0) throw new Error(result.stderr);
await access(binary);
console.log(`Verified and installed ${binary}`);
