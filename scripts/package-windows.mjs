import { access, copyFile, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { root } from "./lib/modding.mjs";

if (process.platform !== "win32")
  throw new Error("Build the Windows package on Windows (or use CI).");
const version = JSON.parse(await readFile(path.join(root, "package.json"), "utf8")).version;
const binary = path.join(root, "native/build/drive-mad.exe");
const release = path.join(root, "native/build/release/windows");
const stage = path.join(release, "stage");
await access(binary).catch(() => {
  throw new Error("Native Windows executable missing; run npm run native:build first.");
});
await rm(release, { recursive: true, force: true });
await mkdir(stage, { recursive: true });
await copyFile(binary, path.join(stage, "drive-mad.exe"));
await copyFile(path.join(root, "packaging/icon.ico"), path.join(stage, "drive-mad.ico"));
await writeFile(
  path.join(stage, "README.txt"),
  `Drive Mad ${version} (native Windows x64)

Launch drive-mad.exe. The game embeds its assets; graphics drivers provide
OpenGL 2.1. Saves live in %LOCALAPPDATA%\\drive-mad-native (override with
DRIVE_MAD_SAVE_DIR). Plugins are opt-in with --mods <folder>.
`,
);

const msys = process.env.MSYS2_ROOT || "C:\\msys64";
const bin = path.join(msys, "mingw64", "bin");
const libraries = [
  "SDL2.dll",
  "libpng16-16.dll",
  "zlib1.dll",
  "libgcc_s_seh-1.dll",
  "libstdc++-6.dll",
  "libwinpthread-1.dll",
  "libssp-0.dll",
];
let copied = 0;
for (const name of libraries) {
  try {
    await copyFile(path.join(bin, name), path.join(stage, name));
    copied++;
  } catch {
    // Optional runtime DLLs vary with the compiler/runtime version.
  }
}
if (!copied)
  throw new Error(`No runtime DLLs found under ${bin}`);
const zip = path.join(release, `DriveMad-${version}-windows-x64.zip`);
const archive = spawnSync("tar", ["-a", "-c", "-f", zip, "-C", stage, "."], {
  encoding: "utf8",
});
if (archive.status !== 0)
  throw new Error(archive.error?.message || archive.stderr || "zip failed");
console.log(stage);
console.log(zip);
