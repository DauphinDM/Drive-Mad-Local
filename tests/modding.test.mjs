import test from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, mkdir, cp, readFile, writeFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { deflateSync } from "node:zlib";
import wabtFactory from "wabt";
import {
  root,
  loadInputs,
  readPackage,
  replacePackage,
  recoverSymbols,
  decodeAsset,
  encodeAsset,
  assetPath,
  sha256,
} from "../scripts/lib/modding.mjs";

test("all real asset ranges and recovered symbols are valid", async () => {
  const { loader, wasm, metadata } = await loadInputs();
  assert.equal(metadata.files.length, 261);
  const symbols = recoverSymbols(loader);
  assert.equal(Object.keys(symbols.exports).length, 76);
  assert.equal(Object.keys(symbols.imports).length, 116);
  const module = new WebAssembly.Module(wasm);
  for (const item of WebAssembly.Module.imports(module)) {
    assert.ok(symbols.imports[item.name], `Unmapped import ${item.name}`);
  }
  assert.deepEqual(readPackage(replacePackage(loader, metadata)), metadata);
});

test("asset codecs preserve data and path traversal is rejected", () => {
  const data = Buffer.from('{"hello":"world"}');
  assert.deepEqual(decodeAsset(deflateSync(data)).data, data);
  assert.equal(decodeAsset(data).encoding, "raw");
  assert.deepEqual(decodeAsset(encodeAsset(data, "zlib")).data, data);
  for (const name of [
    "/assets/../escape",
    "/assets/./escape",
    "/other/file",
    "/assets/a\\b",
    "/assets//b",
  ]) {
    assert.throws(() => assetPath("/tmp/example", name));
  }
});

function run(directory, script, ...args) {
  const result = spawnSync(
    process.execPath,
    [path.join(directory, "scripts", script), ...args],
    {
      cwd: directory,
      encoding: "utf8",
      maxBuffer: 4 * 1024 * 1024,
    },
  );
  if (result.error) throw result.error;
  assert.equal(result.status, 0, result.stdout + result.stderr);
  return result;
}

test("CLI asset roundtrip is byte-identical; changed JSON is recompressed with new offsets", async () => {
  const directory = await mkdtemp(path.join(tmpdir(), "drive-mad-assets-"));
  try {
    await cp(path.join(root, "scripts"), path.join(directory, "scripts"), {
      recursive: true,
    });
    await mkdir(path.join(directory, "webapp"));
    for (const name of ["index.js", "index.data", "index.wasm"]) {
      await cp(
        path.join(root, "webapp", name),
        path.join(directory, "webapp", name),
      );
    }
    const dataPath = path.join(directory, "webapp/index.data");
    const loaderPath = path.join(directory, "webapp/index.js");
    const original = await readFile(dataPath);
    const loader = await readFile(loaderPath, "utf8");
    run(directory, "assets.mjs", "unpack");
    run(directory, "assets.mjs", "pack", "--apply");
    assert.deepEqual(await readFile(dataPath), original);
    assert.equal(await readFile(loaderPath, "utf8"), loader);
    const dbPath = path.join(directory, "modding/generated/unpacked/assets/db");
    const db = JSON.parse(await readFile(dbPath, "utf8"));
    db.moddingTest = "changed-length JSON asset";
    await writeFile(dbPath, JSON.stringify(db, null, 2));
    run(directory, "assets.mjs", "pack", "--apply");
    const packed = await readFile(dataPath);
    const metadata = readPackage(await readFile(loaderPath, "utf8"));
    const file = metadata.files.find((f) => f.filename === "/assets/db");
    assert.deepEqual(
      JSON.parse(decodeAsset(packed.subarray(file.start, file.end)).data),
      db,
    );
    assert.equal(metadata.remote_package_size, packed.length);
    for (const entry of metadata.files.filter(
      (f) => f.filename !== "/assets/db",
    )) {
      const baseline = readPackage(loader).files.find(
        (f) => f.filename === entry.filename,
      );
      assert.deepEqual(
        packed.subarray(entry.start, entry.end),
        original.subarray(baseline.start, baseline.end),
      );
    }
    // A second extraction must not overwrite user edits.
    const repeat = spawnSync(process.execPath, [
      path.join(directory, "scripts/assets.mjs"),
      "unpack",
    ]);
    assert.notEqual(repeat.status, 0);
    assert.equal(
      JSON.parse(await readFile(dbPath, "utf8")).moddingTest,
      db.moddingTest,
    );
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test("the actual WASM roundtrips through WAT with the same ABI and version", async () => {
  const { wasm, loader } = await loadInputs();
  const wabt = await wabtFactory();
  const parsed = wabt.readWasm(wasm, {});
  let roundtripped;
  try {
    parsed.generateNames();
    parsed.applyNames();
    const wat = parsed.toText({});
    const rebuilt = wabt.parseWat("roundtrip.wat", wat);
    try {
      rebuilt.resolveNames();
      rebuilt.validate();
      roundtripped = rebuilt.toBinary({ write_debug_names: true }).buffer;
    } finally {
      rebuilt.destroy();
    }
  } finally {
    parsed.destroy();
  }
  const baseline = new WebAssembly.Module(wasm);
  const rebuilt = new WebAssembly.Module(roundtripped);
  assert.deepEqual(
    WebAssembly.Module.imports(rebuilt),
    WebAssembly.Module.imports(baseline),
  );
  assert.deepEqual(
    WebAssembly.Module.exports(rebuilt),
    WebAssembly.Module.exports(baseline),
  );
  const imports = {};
  for (const item of WebAssembly.Module.imports(baseline)) {
    assert.equal(item.kind, "function");
    (imports[item.module] ??= {})[item.name] = () => 0;
  }
  const versionExport = Object.entries(recoverSymbols(loader).exports).find(
    ([, name]) => name === "get_app_version",
  )[0];
  assert.equal(
    new WebAssembly.Instance(baseline, imports).exports[versionExport](),
    166,
  );
  assert.equal(
    new WebAssembly.Instance(rebuilt, imports).exports[versionExport](),
    166,
  );
  assert.equal(
    sha256(wasm),
    sha256(await readFile(path.join(root, "webapp/index.wasm"))),
  );
});
