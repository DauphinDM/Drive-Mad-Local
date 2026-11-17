import {
  access,
  mkdir,
  readFile,
  writeFile,
  cp,
  rename,
} from "node:fs/promises";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { deflateSync } from "node:zlib";
import { root, loadInputs } from "./lib/modding.mjs";
const [action, argument, extra] = process.argv.slice(2);
const sdk = path.join(root, "native/sdk");
function run(command, args) {
  const result = spawnSync(command, args, { stdio: "inherit" });
  if (result.error) throw result.error;
  if (result.status !== 0)
    throw new Error(`${command} exited ${result.status}`);
}
function ini(text) {
  return Object.fromEntries(
    text
      .split(/\r?\n/)
      .map((line) => line.trim())
      .filter((line) => line && !/^[#;]/.test(line))
      .map((line) => {
        const equals = line.indexOf("=");
        if (equals < 1) throw new Error("Expected key=value in mod.ini");
        return [line.slice(0, equals).trim(), line.slice(equals + 1).trim()];
      }),
  );
}
if (action === "init" && argument) {
  const directory = path.resolve(argument);
  const id = extra || path.basename(directory);
  if (!/^[A-Za-z0-9_-][A-Za-z0-9_.-]{0,62}$/.test(id) || id.includes(".."))
    throw new Error(
      "Use a mod ID of 1–63 letters, digits, _, - or . (not ..).",
    );
  if (
    await access(directory).then(
      () => true,
      () => false,
    )
  )
    throw new Error("Target exists; refusing to overwrite it.");
  await mkdir(path.dirname(directory), { recursive: true });
  await mkdir(directory);
  await cp(path.join(sdk, "examples/native-tools"), directory, {
    recursive: true,
  });
  for (const filename of ["mod.ini", "mod.c"])
    await writeFile(
      path.join(directory, filename),
      (await readFile(path.join(directory, filename), "utf8"))
        .replaceAll("native-tools", id)
        .replaceAll("Native Tools", id),
    );
  console.log(
    `Created ${directory}. Edit mod.c/settings.ini, then: npm run native:mod -- build "${directory}"`,
  );
} else if (action === "build" && argument) {
  const directory = path.resolve(argument);
  const manifest = ini(await readFile(path.join(directory, "mod.ini"), "utf8"));
  if (!manifest.library) {
    console.log("Data-only mod: nothing to compile.");
    process.exit(0);
  }
  if (
    !/^[A-Za-z0-9_.-]+\.so$/.test(manifest.library) ||
    manifest.library.includes("..")
  )
    throw new Error("library must be a .so basename");
  const cpp = await access(path.join(directory, "mod.cpp")).then(
    () => true,
    () => false,
  );
  const source = path.join(directory, cpp ? "mod.cpp" : "mod.c");
  const temp = path.join(directory, `.${manifest.library}.building`);
  run(cpp ? process.env.CXX || "c++" : process.env.CC || "cc", [
    "-shared",
    "-fPIC",
    "-fvisibility=hidden",
    cpp ? "-std=c++17" : "-std=c11",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-Wl,--no-undefined",
    "-I",
    path.join(sdk, "include"),
    source,
    "-o",
    temp,
  ]);
  await rename(temp, path.join(directory, manifest.library));
  console.log(
    `Built ${path.join(directory, manifest.library)}. Restart the game to load it.`,
  );
} else if (action === "asset" && argument && extra) {
  if (!process.argv[5])
    throw new Error(
      "Usage: native:mod -- asset <mod-dir> </assets/path> <decoded-file>",
    );
  const filename = extra;
  const input = process.argv[5];
  const { metadata, data } = await loadInputs();
  const file = metadata.files.find((file) => file.filename === filename);
  if (!file) throw new Error("Unknown game asset path");
  const { decodeAsset } = await import("./lib/modding.mjs");
  const encoding = decodeAsset(data.subarray(file.start, file.end)).encoding;
  const bytes = await readFile(path.resolve(input));
  const output = path.join(path.resolve(argument), filename.slice(1));
  await mkdir(path.dirname(output), { recursive: true });
  await writeFile(
    output,
    encoding === "zlib" ? deflateSync(bytes, { level: 9 }) : bytes,
  );
  console.log(`Wrote ${output} (${encoding}). Restart to load the override.`);
} else if (action === "pack" && argument && extra) {
  const output = path.resolve(extra);
  if (
    await access(output).then(
      () => true,
      () => false,
    )
  )
    throw new Error("Archive exists; refusing to overwrite.");
  const directory = path.resolve(argument);
  if (output === directory || output.startsWith(directory + path.sep))
    throw new Error("Write the archive outside the mod folder.");
  await access(path.join(directory, "mod.ini"));
  run("tar", [
    "-czf",
    output,
    "-C",
    path.dirname(directory),
    path.basename(directory),
  ]);
  console.log(
    `Packed ${output}. Contains native code; share only with users who trust it.`,
  );
} else {
  console.log(
    "Native mod SDK:\n  init <new-directory> [id]\n  build <mod-directory>\n  asset <mod-directory> </assets/path> <decoded-source-file>\n  pack <mod-directory> <output.tar.gz>",
  );
  if (action) process.exitCode = 2;
}
