import { mkdir, readFile, writeFile, access } from "node:fs/promises";
import path from "node:path";
import { root, sha256, writeJSON } from "./lib/modding.mjs";

// Dear ImGui v1.91.9b, MIT. Immutable upstream commit, not a moving branch.
const commit = "f5befd2d29e66809cd1110a152e375a7f1981f06";
const directory = path.join(root, "native/vendor/imgui");
const files = [
  "LICENSE.txt",
  "imgui.h",
  "imconfig.h",
  "imgui_internal.h",
  "imstb_rectpack.h",
  "imstb_textedit.h",
  "imstb_truetype.h",
  "imgui.cpp",
  "imgui_draw.cpp",
  "imgui_tables.cpp",
  "imgui_widgets.cpp",
  "backends/imgui_impl_sdl2.h",
  "backends/imgui_impl_sdl2.cpp",
  "backends/imgui_impl_opengl2.h",
  "backends/imgui_impl_opengl2.cpp",
  "backends/imgui_impl_opengl3.h",
  "backends/imgui_impl_opengl3.cpp",
];
let manifest;
try {
  manifest = JSON.parse(
    await readFile(path.join(directory, "source.json"), "utf8"),
  );
  if (manifest.commit !== commit)
    throw new Error(
      "Different ImGui version exists; preserve it before installing.",
    );
  for (const [file, hash] of Object.entries(manifest.sha256)) {
    if (
      sha256(await readFile(path.join(directory, file))) !== hash
    )
      throw new Error(`ImGui file changed: ${file}; refusing to overwrite it.`);
  }
} catch (error) {
  if (error.code !== "ENOENT") throw error;
  // Do not overwrite even a partial download or local source edits.
  for (const file of files) {
    if (
      await access(path.join(directory, file))
        .then(() => true)
        .catch(() => false)
    )
      throw new Error(
        "Partial ImGui directory exists; back it up before installing.",
      );
  }
  manifest = { version: "1.91.9b", commit, sha256: {} };
}
const missing = [];
for (const file of files) {
  if (
    await access(path.join(directory, file))
      .then(() => true)
      .catch(() => false)
  )
    continue;
  if (manifest.sha256[file])
    throw new Error(`ImGui file is missing: ${file}; refusing to guess.`);
  missing.push(file);
}
if (missing.length) {
  const sources = await Promise.all(
    missing.map(async (file) => {
      const response = await fetch(
        `https://raw.githubusercontent.com/ocornut/imgui/${commit}/${file}`,
      );
      if (!response.ok)
        throw new Error(`ImGui download failed: ${file} (${response.status})`);
      return [file, Buffer.from(await response.arrayBuffer())];
    }),
  );
  for (const [file, bytes] of sources) {
    await mkdir(path.dirname(path.join(directory, file)), { recursive: true });
    await writeFile(path.join(directory, file), bytes);
    manifest.sha256[file] = sha256(bytes);
  }
  await writeJSON(path.join(directory, "source.json"), manifest);
  console.log(
    `Installed ${missing.length} pinned Dear ImGui source file(s); MIT license included.`,
  );
} else {
  console.log("Pinned Dear ImGui is already installed.");
}
