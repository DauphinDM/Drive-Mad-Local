import { mkdir, readFile, writeFile, readdir, access } from "node:fs/promises";
import path from "node:path";
import os from "node:os";
import { spawn } from "node:child_process";
import { nativeExports } from "./lib/native-exports.mjs";
import wabtFactory from "wabt";
import {
  supportsGameplayProfile,
  gameplayExports,
  gameplayPrivateBindings,
} from "./lib/native-gameplay.mjs";
import {
  root,
  generated,
  loadInputs,
  recoverSymbols,
  sha256,
  writeJSON,
} from "./lib/modding.mjs";

const platform = process.env.DM_TARGET_OS || process.platform;
if (!["linux", "darwin", "win32"].includes(platform))
  throw new Error(`Unsupported native target: ${platform}`);
const windows = platform === "win32";
const macos = platform === "darwin";
// Linux keeps the original GLES2 path. macOS and Windows use desktop GL 2.1
// with a shader compatibility layer. DM_DESKTOP_GL=1 on Linux exercises the
// same path locally for validation.
const desktopGL = windows || macos || process.env.DM_DESKTOP_GL === "1";
const output = path.join(root, "native/build");
const wabt = path.join(generated, "tools/wabt-1.0.39");
const translator =
  process.env.WASM2C ||
  path.join(wabt, "bin", process.platform === "win32" ? "wasm2c.exe" : "wasm2c");
const runtime =
  process.env.WASM2C_RUNTIME || path.join(wabt, "share/wabt/wasm2c");
const include = process.env.WASM2C_INCLUDE || path.join(wabt, "include");
await access(translator).catch(() => {
  throw new Error("Run npm run tools:install first (native WABT is required).");
});
await mkdir(output, { recursive: true });
function run(command, args, capture = false) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, {
      cwd: root,
      stdio: capture ? ["ignore", "pipe", "inherit"] : "inherit",
    });
    let text = "";
    if (capture)
      child.stdout.on("data", (data) => {
        text += data;
      });
    child.on("error", reject);
    child.on("exit", (code) =>
      code === 0
        ? resolve(text.trim())
        : reject(new Error(`${command} exited ${code}`)),
    );
  });
}
// Always consume the actual editable WAT/workspace, not an old alternate binary.
await run(process.execPath, [
  path.join(root, "scripts/build-wasm.mjs"),
  ...process.argv.slice(2),
]);
await run(translator, [
  path.join(root, "webapp/index.modded.wasm"),
  "-n",
  "game",
  "-o",
  path.join(output, "game.c"),
  "--num-outputs=8",
]);
const header = await readFile(path.join(output, "game.h"), "utf8");
const { loader, data, metadata, wasm } = await loadInputs();
const symbols = recoverSymbols(loader);
const aliases = [
  "/* Generated ABI aliases; do not edit. */",
  '#include "game.h"',
];
// The loader keeps this runtime helper private (not a Module export wrapper).
const tempRet = loader.match(
  /__emscripten_tempret_set=wasmExports\["([^"]+)"\]/,
)?.[1];
const tempRetC = [
  ...header.matchAll(/\/\* export: '([^']+)' \*\/\s+[\w* ]+ (w2c_game_\w+)\(/g),
].find((match) => match[1] === tempRet)?.[2];
if (!tempRetC)
  throw new Error("Cannot identify the Emscripten i64 return helper.");
aliases.push(`#define game_emscripten_tempret_set ${tempRetC}`);
for (const match of header.matchAll(
  /\/\* import: 'a' '([^']+)' \*\/\s+\w+ (w2c_a_\w+)\(/g,
)) {
  const name = symbols.imports[match[1]];
  if (!name) throw new Error(`No recovered import name for ${match[1]}`);
  aliases.push(`#define dm_${name.replace(/^_+/, "")} ${match[2]}`);
}
for (const match of header.matchAll(
  /\/\* export: '([^']+)' \*\/\s+[\w* ]+ (w2c_game_\w+)\(/g,
)) {
  const name = symbols.exports[match[1]];
  if (name) aliases.push(`#define game_${name} ${match[2]}`);
}
await writeFile(path.join(output, "abi.h"), aliases.join("\n") + "\n");
const sdkExports = nativeExports(header, symbols);
await writeFile(path.join(output, "sdk-exports.c"), sdkExports.source);
await writeJSON(path.join(output, "sdk-exports.json"), [
  ...sdkExports.exports,
  ...gameplayExports(
    await readFile(path.join(root, "native/gameplay.c"), "utf8"),
  ),
]);
const wabtJS = await wabtFactory();
const compiledWasm = await readFile(
  path.join(root, "webapp/index.modded.wasm"),
);
const gameplayProfile = supportsGameplayProfile(wabtJS, wasm, compiledWasm);
await writeFile(
  path.join(output, "gameplay-profile.h"),
  `/* Verified executable-layout profile. Do not hand-enable for edited code. */\n#define DM_GAMEPLAY_PROFILE ${gameplayProfile ? 1 : 0}\n` +
    (gameplayProfile
      ? gameplayPrivateBindings(
          await readFile(path.join(output, "game-impl.h"), "utf8"),
        )
      : ""),
);
// Embed the packed, still-compressed asset bytes; the original game decodes them.
const blob = [];
for (let start = 0; start < data.length; start += 4096) {
  blob.push(
    `"${[...data.subarray(start, start + 4096)].map((byte) => `\\x${byte.toString(16).padStart(2, "0")}`).join("")}"`,
  );
}
await writeFile(
  path.join(output, "assets.c"),
  [
    '#include "host.h"',
    `const unsigned char native_assets[] =\n${blob.join("\n")};`,
    "const NativeAsset native_asset_index[] = {",
    ...metadata.files.map(
      (file) =>
        `{${JSON.stringify(file.filename)}, ${file.start}, ${file.end - file.start}},`,
    ),
    "};",
    `const size_t native_asset_count = ${metadata.files.length};`,
  ].join("\n"),
);
const packages = ["sdl2", "libpng"];
if (desktopGL && !macos) packages.push("gl");
if (!desktopGL) packages.push("glesv2");
const flags = (
  await run("pkg-config", ["--cflags", "--libs", ...packages], true)
).split(/\s+/);
const cc = process.env.CC || "cc";
const cxx = process.env.CXX || "c++";
const common = [
  ...(windows ? [] : ["-D_GNU_SOURCE"]),
  ...(desktopGL ? ["-DDM_DESKTOP_GL"] : []),
  ...(macos ? ["-DGL_SILENCE_DEPRECATION"] : []),
  "-DWASM_RT_USE_SEGUE=0",
  "-O2",
  "-fno-strict-aliasing",
  "-I" + include,
  "-I" + runtime,
  "-I" + output,
  "-I" + path.join(root, "native"),
  "-I" + path.join(root, "native/vendor/imgui"),
  ...(desktopGL ? [] : ["-DIMGUI_IMPL_OPENGL_ES2"]),
  ...flags.filter((flag) => !flag.startsWith("-l")),
];
const files = (await readdir(output))
  .filter((name) => /^game(?:_\d+)?\.c$/.test(name))
  .map((name) => path.join(output, name));
files.push(
  path.join(output, "assets.c"),
  path.join(output, "sdk-exports.c"),
  ...[
    "main.c",
    "graphics.c",
    "platform.c",
    "filesystem.c",
    "mods.c",
    "gameplay.c",
    "gl_loader.c",
    ...(windows ? ["win32_compat.c"] : []),
    "menu.cpp",
  ].map((name) => path.join(root, "native", name)),
  path.join(runtime, "wasm-rt-impl.c"),
  path.join(runtime, "wasm-rt-mem-impl.c"),
);
const imgui = path.join(root, "native/vendor/imgui");
for (const name of [
  "imgui.cpp",
  "imgui_draw.cpp",
  "imgui_tables.cpp",
  "imgui_widgets.cpp",
  "backends/imgui_impl_sdl2.cpp",
  desktopGL ? "backends/imgui_impl_opengl2.cpp" : "backends/imgui_impl_opengl3.cpp",
]) {
  await access(path.join(imgui, name)).catch(() => {
    throw new Error(
      "Native UI sources missing. Run npm run native:ui:install.",
    );
  });
  files.push(path.join(imgui, name));
}
const objects = files.map((file) =>
  path.join(output, path.basename(file).replace(/\.(?:c|cpp)$/, ".o")),
);
const cachePath = path.join(output, "object-cache.json");
const cache = await readFile(cachePath, "utf8")
  .then(JSON.parse)
  .catch(() => ({}));
const dependencyFiles = [
  path.join(output, "game.h"),
  path.join(output, "game-impl.h"),
  path.join(output, "abi.h"),
  path.join(root, "native/host.h"),
  path.join(root, "native/gl_compat.h"),
  path.join(root, "native/win32_compat.h"),
  path.join(root, "native/gameplay.h"),
  path.join(output, "gameplay-profile.h"),
  path.join(root, "native/sdk/include/drive_mad.h"),
];
for (const directory of [imgui, path.join(imgui, "backends")])
  for (const name of await readdir(directory))
    if (name.endsWith(".h")) dependencyFiles.push(path.join(directory, name));
for (const directory of [runtime, include]) {
  for (const name of await readdir(directory)) {
    if (/\.(h|inc)$/.test(name))
      dependencyFiles.push(path.join(directory, name));
  }
}
const dependencyHash = sha256(
  Buffer.concat([
    Buffer.from(
      JSON.stringify(common) +
        (await run(cc, ["--version"], true)) +
        (await run(cxx, ["--version"], true)),
    ),
    ...(await Promise.all(dependencyFiles.map((file) => readFile(file)))),
  ]),
);
const nextCache = {};
const jobs = Math.max(
  1,
  Math.min(Number(process.env.JOBS) || 4, os.availableParallelism(), 8),
);
let next = 0;
console.log(
  `Compiling native C/C++ (${files.length} files, ${jobs} jobs; gameplay profile ${gameplayProfile ? "verified" : "disabled"})…`,
);
await Promise.all(
  Array.from({ length: jobs }, async () => {
    while (next < files.length) {
      const index = next++;
      const hash = sha256(
        Buffer.concat([
          Buffer.from(dependencyHash),
          await readFile(files[index]),
        ]),
      );
      nextCache[objects[index]] = hash;
      if (
        cache[objects[index]] === hash &&
        (await access(objects[index])
          .then(() => true)
          .catch(() => false))
      )
        continue;
      const cpp = files[index].endsWith(".cpp");
      await run(cpp ? cxx : cc, [
        ...common,
        cpp ? "-std=c++17" : "-std=c11",
        "-c",
        files[index],
        "-o",
        objects[index],
      ]);
    }
  }),
);
await writeJSON(cachePath, nextCache);
const executable = path.join(output, windows ? "drive-mad.exe" : "drive-mad");
await run(cxx, [
  ...objects,
  ...flags.filter(
    (flag) =>
      flag.startsWith("-l") || flag.startsWith("-L") || flag.startsWith("-Wl,"),
  ),
  ...(macos ? ["-framework", "OpenGL"] : []),
  ...(windows ? ["-lopengl32"] : []),
  ...(windows ? [] : ["-lm"]),
  ...(platform === "linux" ? ["-ldl"] : []),
  "-o",
  executable,
]);
await writeJSON(path.join(output, "build.json"), {
  platform,
  desktopGL,
  architecture: os.arch(),
  originalWasmSha256: sha256(wasm),
  compiledWasmSha256: sha256(
    await readFile(path.join(root, "webapp/index.modded.wasm")),
  ),
  assetsSha256: sha256(data),
  translator: await run(translator, ["--version"], true),
  gameplayProfile,
  nativeUi: JSON.parse(await readFile(path.join(imgui, "source.json"), "utf8")),
});
console.log(
  `Built ${path.relative(root, executable)} (${platform}${desktopGL ? ", desktop GL" : ", GLES2"}, embedded assets, no web runtime).`,
);
