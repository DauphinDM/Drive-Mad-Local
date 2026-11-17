import {
  access,
  copyFile,
  mkdir,
  readFile,
  readdir,
  rm,
  writeFile,
} from "node:fs/promises";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { root } from "./lib/modding.mjs";

if (process.platform !== "win32")
  throw new Error("Build the Windows package on Windows (or use CI).");
const version = JSON.parse(
  await readFile(path.join(root, "package.json"), "utf8"),
).version;
const binary = path.join(root, "native/build/drive-mad.exe");
const release = path.join(root, "native/build/release/windows");
const stage = path.join(release, "stage");
await access(binary).catch(() => {
  throw new Error(
    "Native Windows executable missing; run npm run native:build first.",
  );
});
await rm(release, { recursive: true, force: true });
await mkdir(stage, { recursive: true });
await copyFile(binary, path.join(stage, "drive-mad.exe"));
await copyFile(
  path.join(root, "packaging/icon.ico"),
  path.join(stage, "drive-mad.ico"),
);
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
// Follow PE imports recursively: libcurl's TLS/compression dependencies vary
// between MSYS releases. Windows system DLLs remain provided by Windows.
const available = new Map(
  (await readdir(bin)).map((name) => [name.toLowerCase(), name]),
);
const copied = new Set();
async function collect(file) {
  const result = spawnSync(path.join(bin, "objdump.exe"), ["-p", file], {
    encoding: "utf8",
  });
  if (result.status !== 0)
    throw new Error(
      result.error?.message || result.stderr || "Could not inspect DLL imports",
    );
  for (const [, imported] of result.stdout.matchAll(
    /DLL Name:\s+([^\r\n]+)/g,
  )) {
    const key = imported.trim().toLowerCase();
    const name = available.get(key);
    if (!name || copied.has(key)) continue;
    copied.add(key);
    const target = path.join(stage, name);
    await copyFile(path.join(bin, name), target);
    await collect(target);
  }
}
await collect(path.join(stage, "drive-mad.exe"));
if (![...copied].some((name) => name.startsWith("libcurl")))
  throw new Error("libcurl was not bundled with the Windows build");
const zip = path.join(release, `DriveMad-${version}-windows-x64.zip`);
// Use bsdtar from Windows rather than Git's GNU tar (which lacks -a).
const tar = `${process.env.SystemRoot || "C:\\Windows"}\\System32\\tar.exe`;
const archive = spawnSync(tar, ["-a", "-c", "-f", zip, "-C", stage, "."], {
  encoding: "utf8",
});
if (archive.status !== 0)
  throw new Error(archive.error?.message || archive.stderr || "zip failed");
console.log(stage);
console.log(zip);
