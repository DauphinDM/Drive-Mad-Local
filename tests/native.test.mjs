import test from "node:test";
import assert from "node:assert/strict";
import { access, mkdtemp, readFile, rm } from "node:fs/promises";
import { spawnSync } from "node:child_process";
import path from "node:path";
import os from "node:os";
import { root } from "../scripts/lib/modding.mjs";

const binary = path.join(root, "native/build/drive-mad");
const built =
  process.platform === "linux" &&
  (await access(binary)
    .then(() => true)
    .catch(() => false));
test(
  "native ELF runs outside the repo and validates embedded assets, save I/O and path confinement",
  {
    skip:
      !built &&
      "Build with npm run native:build to enable this integration test.",
  },
  async () => {
    const header = (await readFile(binary)).subarray(0, 5);
    assert.deepEqual([...header], [127, 69, 76, 70, 2]);
    const directory = await mkdtemp(
      path.join(
        process.env.OPENCODE_TMP_DIR || os.tmpdir(),
        "drive-mad-native-",
      ),
    );
    try {
      const result = spawnSync(binary, ["--self-test"], {
        cwd: directory,
        env: { ...process.env, DRIVE_MAD_SAVE_DIR: directory },
        encoding: "utf8",
        timeout: 30000,
      });
      assert.equal(
        result.status,
        0,
        result.error?.message || result.stdout + result.stderr,
      );
      assert.match(
        result.stdout,
        /Native self-test passed\. Compiled game version: \d+/,
      );
    } finally {
      await rm(directory, { recursive: true, force: true });
    }
  },
);
