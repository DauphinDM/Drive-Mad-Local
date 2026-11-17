import {
  access,
  chmod,
  copyFile,
  mkdir,
  readFile,
  readdir,
  rm,
  writeFile,
} from "node:fs/promises";
import path from "node:path";
import { spawn } from "node:child_process";
import { root } from "./lib/modding.mjs";

if (process.platform !== "linux")
  throw new Error(
    "Build Linux packages on Linux (or use the GitHub Actions workflow).",
  );
const version = JSON.parse(
  await readFile(path.join(root, "package.json"), "utf8"),
).version;
const binary = path.join(root, "native/build/drive-mad");
const release = path.join(root, "native/build/release/linux");
const appdir = path.join(release, "DriveMad.AppDir");
const packageDir = path.join(release, `DriveMad-${version}-linux-x86_64`);
const appimage = path.join(release, `DriveMad-${version}-x86_64.AppImage`);
const tarball = path.join(release, `DriveMad-${version}-linux-x86_64.tar.gz`);
const tools = path.join(release, "tools");
await access(binary).catch(() => {
  throw new Error("Native binary missing; run npm run native:build first.");
});

function run(command, args, options = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, {
      cwd: root,
      stdio: options.capture ? ["ignore", "pipe", "inherit"] : "inherit",
      env: { ...process.env, ...options.env },
    });
    let output = "";
    if (options.capture)
      child.stdout.on("data", (data) => {
        output += data;
      });
    child.on("error", reject);
    child.on("exit", (code) =>
      code === 0
        ? resolve(output.trim())
        : reject(new Error(`${command} exited ${code}`)),
    );
  });
}

await rm(release, { recursive: true, force: true });
await mkdir(appdir, { recursive: true });
await mkdir(packageDir, { recursive: true });
await mkdir(tools, { recursive: true });

const desktop = `[Desktop Entry]
Type=Application
Name=Drive Mad
Comment=Native Drive Mad with mod workbench
Exec=drive-mad
Icon=drive-mad
Categories=Game;
Terminal=false
`;
await writeFile(path.join(release, "drive-mad.desktop"), desktop);
await copyFile(
  path.join(root, "packaging/icon.png"),
  path.join(release, "drive-mad.png"),
);

// Bundle non-system libraries; GPU/display drivers stay on the host.
const libraries = new Set();
for (const line of (await run("ldd", [binary], { capture: true })).split(
  "\n",
)) {
  const match = line.match(/=>\s+(\/\S+)/);
  if (!match) continue;
  const name = path.basename(match[1]);
  if (
    /^(lib(c|m|pthread|dl|rt)\.so|ld-linux|libGL|libEGL|libGLX|libGLdispatch|libdrm|libgbm)/.test(
      name,
    )
  )
    continue;
  libraries.add(match[1]);
}
for (const file of [
  "usr/bin/drive-mad",
  "drive-mad.desktop",
  "drive-mad.png",
]) {
  const target = path.join(appdir, file);
  await mkdir(path.dirname(target), { recursive: true });
}
await copyFile(binary, path.join(appdir, "usr/bin/drive-mad"));
await writeFile(
  path.join(appdir, "AppRun"),
  `#!/bin/sh
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$HERE/usr/lib\${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/usr/bin/drive-mad" "$@"
`,
  { mode: 0o755 },
);
await chmod(path.join(appdir, "AppRun"), 0o755);
await copyFile(
  path.join(release, "drive-mad.desktop"),
  path.join(appdir, "drive-mad.desktop"),
);
await copyFile(
  path.join(release, "drive-mad.png"),
  path.join(appdir, "drive-mad.png"),
);
await mkdir(path.join(appdir, "usr/share/icons/hicolor/512x512/apps"), {
  recursive: true,
});
await copyFile(
  path.join(release, "drive-mad.png"),
  path.join(appdir, "usr/share/icons/hicolor/512x512/apps/drive-mad.png"),
);
await mkdir(path.join(appdir, "usr/share/applications"), { recursive: true });
await copyFile(
  path.join(release, "drive-mad.desktop"),
  path.join(appdir, "usr/share/applications/drive-mad.desktop"),
);
await mkdir(path.join(appdir, "usr/lib"), { recursive: true });
for (const library of libraries)
  await copyFile(library, path.join(appdir, "usr/lib", path.basename(library)));

// Portable tarball with a user-level installer.
await copyFile(binary, path.join(packageDir, "drive-mad"));
await copyFile(
  path.join(release, "drive-mad.desktop"),
  path.join(packageDir, "drive-mad.desktop"),
);
await copyFile(
  path.join(release, "drive-mad.png"),
  path.join(packageDir, "drive-mad.png"),
);
await writeFile(
  path.join(packageDir, "install.sh"),
  `#!/bin/sh
set -eu
PREFIX="\${PREFIX:-$HOME/.local}"
install -Dm755 drive-mad "$PREFIX/bin/drive-mad"
install -Dm644 drive-mad.desktop "$PREFIX/share/applications/drive-mad.desktop"
install -Dm644 drive-mad.png "$PREFIX/share/icons/hicolor/512x512/apps/drive-mad.png"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$PREFIX/share/applications" || true
echo "Installed $PREFIX/bin/drive-mad"
`,
  { mode: 0o755 },
);
await chmod(path.join(packageDir, "install.sh"), 0o755);
await writeFile(
  path.join(packageDir, "README.txt"),
  `Drive Mad ${version} (native Linux x86-64)

Run install.sh for a user-level install in ~/.local, or copy drive-mad to
~/.local/bin and the desktop/icon files into the matching XDG folders.
The game embeds its assets; GL/EGL drivers come from your system.
Saves: ~/.local/share/drive-mad-native (override with DRIVE_MAD_SAVE_DIR).
`,
);
await run("tar", ["-czf", tarball, "-C", release, path.basename(packageDir)]);

// appimagetool itself is an AppImage. Extract-and-run avoids a FUSE dependency.
const appimagetool = path.join(tools, "appimagetool");
try {
  await access(appimagetool);
} catch {
  const response = await fetch(
    "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage",
  );
  if (!response.ok)
    throw new Error(`appimagetool download failed (${response.status})`);
  await writeFile(appimagetool, Buffer.from(await response.arrayBuffer()));
  await chmod(appimagetool, 0o755);
}
await run(appimagetool, [appdir, appimage], {
  env: { APPIMAGE_EXTRACT_AND_RUN: "1", ARCH: "x86_64", VERSION: version },
});
await chmod(appimage, 0o755);
console.log(appimage);
console.log(tarball);
