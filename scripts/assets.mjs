import { mkdir, readFile, writeFile, access } from "node:fs/promises";
import path from "node:path";
import {
  root,
  generated,
  sha256,
  loadInputs,
  assetPath,
  decodeAsset,
  encodeAsset,
  replacePackage,
  validatePackage,
  writeJSON,
} from "./lib/modding.mjs";

const command = process.argv[2];
const directory = path.join(generated, "unpacked");
const manifestPath = path.join(directory, "manifest.json");

if (command === "unpack") {
  try {
    await access(directory);
    throw new Error(
      "Unpacked directory exists; move it elsewhere before extracting again (your edits are protected).",
    );
  } catch (error) {
    if (error.code !== "ENOENT") throw error;
  }
  const { loader, data, metadata } = await loadInputs();
  await mkdir(directory, { recursive: true });
  // Baseline is also the source for byte-identical unchanged compressed files.
  await writeFile(path.join(directory, "original.data"), data);
  await writeFile(path.join(directory, "original-loader.js"), loader);
  const files = [];
  for (const file of metadata.files) {
    const bytes = data.subarray(file.start, file.end);
    const decoded = decodeAsset(bytes);
    const output = assetPath(directory, file.filename);
    await mkdir(path.dirname(output), { recursive: true });
    await writeFile(output, decoded.data);
    files.push({
      ...file,
      encoding: decoded.encoding,
      decodedSha256: sha256(decoded.data),
      decodedSize: decoded.data.length,
    });
  }
  await writeJSON(manifestPath, {
    version: 1,
    dataSha256: sha256(data),
    loaderSha256: sha256(loader),
    files,
  });
  console.log(
    `Extracted ${files.length} assets to ${path.relative(root, directory)}/assets/`,
  );
  console.log(
    "Zlib assets are decompressed automatically. Edit them, then run npm run assets:pack.",
  );
} else if (command === "pack") {
  const manifest = JSON.parse(await readFile(manifestPath, "utf8"));
  if (manifest.version !== 1)
    throw new Error("Unsupported asset manifest version");
  const original = await readFile(path.join(directory, "original.data"));
  const baselineLoader = await readFile(
    path.join(directory, "original-loader.js"),
    "utf8",
  );
  if (
    sha256(original) !== manifest.dataSha256 ||
    sha256(baselineLoader) !== manifest.loaderSha256
  ) {
    throw new Error("Extraction baseline was modified; refusing to repack.");
  }
  validatePackage(
    { files: manifest.files, remote_package_size: original.length },
    original,
  );
  const parts = [];
  const files = [];
  let offset = 0;
  let changed = 0;
  for (const file of manifest.files) {
    const decoded = await readFile(assetPath(directory, file.filename));
    const unchanged = sha256(decoded) === file.decodedSha256;
    const bytes = unchanged
      ? original.subarray(file.start, file.end)
      : encodeAsset(decoded, file.encoding);
    if (!unchanged) changed++;
    const { encoding, decodedSha256, decodedSize, ...metadata } = file;
    files.push({ ...metadata, start: offset, end: offset + bytes.length });
    parts.push(bytes);
    offset += bytes.length;
  }
  const data = Buffer.concat(parts);
  const metadata = { files, remote_package_size: data.length };
  validatePackage(metadata, data);
  // Preserve unrelated loader changes (including earlier repacks).
  const currentLoader = await readFile(
    path.join(root, "webapp/index.js"),
    "utf8",
  );
  const loader =
    changed === 0 && sha256(currentLoader) === manifest.loaderSha256
      ? currentLoader
      : replacePackage(currentLoader, metadata);
  if (!process.argv.includes("--apply")) {
    throw new Error(
      "Packing changes webapp/index.data and index.js. Use npm run assets:pack (includes --apply).",
    );
  }
  await writeFile(path.join(root, "webapp/index.data"), data);
  await writeFile(path.join(root, "webapp/index.js"), loader);
  console.log(
    `Packed ${files.length} assets (${changed} changed), ${data.length} bytes. Reload the game.`,
  );
} else {
  throw new Error("Usage: node scripts/assets.mjs unpack | pack --apply");
}
