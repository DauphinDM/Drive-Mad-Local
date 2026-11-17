import { createHash } from "node:crypto";
import { readFile, writeFile, mkdir } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { deflateSync, inflateSync } from "node:zlib";

export const root = fileURLToPath(new URL("../../", import.meta.url));
export const generated = path.join(root, "modding/generated");
export const sha256 = (data) => createHash("sha256").update(data).digest("hex");

const packagePattern = /loadPackage\((\{files:.*?remote_package_size:\d+\})\)/;

// The loader's metadata is a literal, not arbitrary JavaScript. Never eval it.
export function readPackage(loader) {
  const match = loader.match(packagePattern);
  if (!match)
    throw new Error("Cannot find Emscripten asset metadata in index.js");
  return JSON.parse(match[1].replace(/([{,])(\w+):/g, '$1"$2":'));
}

export function replacePackage(loader, metadata) {
  readPackage(loader);
  // Keep the same literal shape so the packer can read its own output.
  const literal = JSON.stringify(metadata).replace(/"(\w+)":/g, "$1:");
  return loader.replace(packagePattern, () => `loadPackage(${literal})`);
}

export function assetPath(base, filename) {
  if (!/^\/assets\/[^\\]+$/.test(filename)) {
    throw new Error(`Invalid asset path: ${filename}`);
  }
  const relative = filename.slice(1);
  if (
    relative.split("/").some((part) => part === ".." || part === "." || !part)
  ) {
    throw new Error(`Invalid asset path: ${filename}`);
  }
  return path.join(base, relative);
}

export function decodeAsset(bytes) {
  // RFC 1950 zlib header; images, WAV and plain JSON stay untouched.
  if (
    bytes.length >= 2 &&
    bytes[0] === 0x78 &&
    ((bytes[0] << 8) + bytes[1]) % 31 === 0
  ) {
    return { encoding: "zlib", data: inflateSync(bytes) };
  }
  return { encoding: "raw", data: bytes };
}

export function encodeAsset(data, encoding) {
  if (encoding === "zlib") return deflateSync(data, { level: 9 });
  if (encoding === "raw") return data;
  throw new Error(`Unknown asset encoding: ${encoding}`);
}

export function validatePackage(metadata, bytes) {
  if (metadata.remote_package_size !== bytes.length) {
    throw new Error("Asset bundle size does not match loader metadata");
  }
  let end = 0;
  const seen = new Set();
  for (const file of metadata.files) {
    assetPath(".", file.filename);
    if (
      seen.has(file.filename) ||
      file.start !== end ||
      !Number.isSafeInteger(file.end) ||
      file.end < file.start ||
      file.end > bytes.length
    ) {
      throw new Error(`Invalid asset range: ${file.filename}`);
    }
    seen.add(file.filename);
    end = file.end;
  }
  if (end !== bytes.length)
    throw new Error("Unaccounted bytes in asset bundle");
}

export function recoverSymbols(loader) {
  const exports = {};
  for (const match of loader.matchAll(
    /Module\["(_[^"]+)"\]=wasmExports\["([^"]+)"\]/g,
  )) {
    exports[match[2]] = match[1].slice(1);
  }
  const imports = {};
  const object = loader.match(/var wasmImports=\{([^}]+)\}/);
  if (!object) throw new Error("Cannot find WASM import mapping");
  for (const entry of object[1].split(",")) {
    const match = entry.match(/^([\w$]+):([\w$]+)$/);
    if (!match) throw new Error(`Unrecognized WASM import: ${entry}`);
    imports[match[1]] = match[2];
  }
  return { imports, exports };
}

export async function writeJSON(filename, data) {
  await mkdir(path.dirname(filename), { recursive: true });
  await writeFile(filename, JSON.stringify(data, null, 2) + "\n");
}

export async function loadInputs() {
  const [loader, wasm, data] = await Promise.all([
    readFile(path.join(root, "webapp/index.js"), "utf8"),
    readFile(path.join(root, "webapp/index.wasm")),
    readFile(path.join(root, "webapp/index.data")),
  ]);
  const metadata = readPackage(loader);
  validatePackage(metadata, data);
  return { loader, wasm, data, metadata };
}
