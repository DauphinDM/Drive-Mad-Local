import { access, mkdir, cp, writeFile } from "node:fs/promises";
import path from "node:path";
import { root } from "./lib/modding.mjs";
const output = path.resolve(
  process.argv[2] || path.join(root, "native/build/sdk"),
);
if (
  await access(output).then(
    () => true,
    () => false,
  )
)
  throw new Error(
    "SDK output exists; choose a fresh directory to protect edits.",
  );
await access(path.join(root, "native/build/sdk-exports.json")).catch(() => {
  throw new Error("Run npm run native:build before exporting the SDK.");
});
await mkdir(output, { recursive: true });
await cp(path.join(root, "native/sdk/include"), path.join(output, "include"), {
  recursive: true,
});
await cp(
  path.join(root, "native/sdk/cmake"),
  path.join(output, "lib/cmake/DriveMadSDK"),
  { recursive: true },
);
await cp(
  path.join(root, "native/sdk/examples"),
  path.join(output, "examples"),
  { recursive: true },
);
await cp(
  path.join(root, "native/sdk/README.md"),
  path.join(output, "README.md"),
);
for (const name of ["GAMEPLAY.md", "GAMEPLAY-RESEARCH.md"])
  await cp(path.join(root, "native/sdk", name), path.join(output, name));
await cp(
  path.join(root, "native/build/sdk-exports.json"),
  path.join(output, "exports.json"),
);
await mkdir(path.join(output, "lib/pkgconfig"), { recursive: true });
await writeFile(
  path.join(output, "lib/pkgconfig/drive-mad-sdk.pc"),
  `prefix=\${pcfiledir}/../..
includedir=\${prefix}/include
Name: Drive Mad Native SDK
Description: Header-only native plugin ABI
Version: 1.0.0
Cflags: -I\${includedir}
Libs:
`,
);
console.log(
  `Exported standalone SDK to ${output} (headers, signatures, CMake, pkg-config, examples).`,
);
