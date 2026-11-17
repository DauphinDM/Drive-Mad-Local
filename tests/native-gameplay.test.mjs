import test from "node:test";
import assert from "node:assert/strict";
import {
  access,
  readFile,
  writeFile,
  mkdir,
  mkdtemp,
  rm,
} from "node:fs/promises";
import { spawnSync } from "node:child_process";
import path from "node:path";
import os from "node:os";
import wabtFactory from "wabt";
import { root } from "../scripts/lib/modding.mjs";
import {
  supportsGameplayProfile,
  gameplayPrivateBindings,
  gameplayExports,
} from "../scripts/lib/native-gameplay.mjs";

const binary = path.join(root, "native/build/drive-mad");
const built =
  process.platform === "linux" &&
  (await access(binary).then(
    () => true,
    () => false,
  ));
const gui =
  built &&
  process.env.DRIVE_MAD_TEST_GUI === "1" &&
  (process.env.DISPLAY || process.env.WAYLAND_DISPLAY);
async function temp(work) {
  const dir = await mkdtemp(
    path.join(
      process.env.OPENCODE_TMP_DIR || os.tmpdir(),
      "drive-mad-gameplay-",
    ),
  );
  try {
    return await work(dir);
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
}
function run(command, args, options = {}) {
  const p = spawnSync(command, args, {
    encoding: "utf8",
    timeout: 45000,
    ...options,
  });
  assert.equal(p.status, 0, p.error?.message || p.stdout + p.stderr);
  return p;
}
test("gameplay signatures are complete and the executable profile fails closed for real logic edits", async () => {
  const entries = gameplayExports(
    await readFile(path.join(root, "native/gameplay.c"), "utf8"),
  );
  assert.equal(entries.length, 40);
  assert.equal(new Set(entries.map((e) => e.name)).size, 40);
  assert.equal(
    entries.find((e) => e.name === "native.profile_supported").flags,
    1,
  );
  assert.deepEqual(
    entries.find((e) => e.name === "native.body_velocity").parameters,
    ["u32", "f32", "f32", "f32"],
  );
  const declarations = Array.from(
    { length: 2302 },
    (_, index) => `void w2c_game_custom_${index + 116}(w2c_game*);`,
  ).join("\n");
  assert.match(
    gameplayPrivateBindings(declarations),
    /#define game_physics_cleanup w2c_game_custom_288/,
  );
  assert.match(
    gameplayPrivateBindings(declarations),
    /#define game_versus_enter w2c_game_custom_1664/,
  );
  assert.throws(
    () =>
      gameplayPrivateBindings(
        declarations.replace(
          "void w2c_game_custom_1664(w2c_game*);",
          "u32 w2c_game_custom_1664(w2c_game*, u32);",
        ),
      ),
    /Versus entry signature/,
  );
  assert.throws(
    () => gameplayPrivateBindings(""),
    /private native function table/,
  );
  assert.throws(
    () =>
      gameplayPrivateBindings(
        declarations.replace(
          "void w2c_game_custom_288(w2c_game*);",
          "u32 w2c_game_custom_288(w2c_game*, u32);",
        ),
      ),
    /teardown signature/,
  );
  const wabt = await wabtFactory();
  const original = await readFile(path.join(root, "webapp/index.wasm"));
  const compiled = await readFile(
    path.join(root, "webapp/index.modded.wasm"),
  ).catch(() => original);
  assert.equal(supportsGameplayProfile(wabt, original, original), true);
  if (built) {
    const info = JSON.parse(
      await readFile(path.join(root, "native/build/build.json"), "utf8"),
    );
    assert.equal(
      supportsGameplayProfile(wabt, original, compiled),
      info.gameplayProfile,
    );
  }
  assert.equal(
    supportsGameplayProfile(wabt, Buffer.from([0, 1, 2]), compiled),
    false,
  );
  const module = wabt.readWasm(Uint8Array.from(original), {
    readDebugNames: false,
  });
  let wat;
  try {
    module.generateNames();
    module.applyNames();
    wat = module.toText({ foldExprs: false, inlineExport: false });
  } finally {
    module.destroy();
  }
  const changed = wat.replace(
    /(\(func \$Vb[^\n]*\n\s*i32.const )166\b/,
    "$1" + "167",
  );
  assert.notEqual(changed, wat);
  const modified = wabt.parseWat("edited.wat", changed);
  try {
    modified.resolveNames();
    modified.validate();
    assert.equal(
      supportsGameplayProfile(
        wabt,
        original,
        modified.toBinary({ write_debug_names: true }).buffer,
      ),
      false,
    );
  } finally {
    modified.destroy();
  }
});
test(
  "native console exposes typed gameplay calls and rejects wrong argument arity",
  { skip: !built },
  async () => {
    await temp(async (dir) => {
      const options = {
        cwd: dir,
        env: { ...process.env, DRIVE_MAD_SAVE_DIR: path.join(dir, "save") },
      };
      const result = run(
        binary,
        [
          "--self-test",
          "--command",
          "api native.profile_supported",
          "--command",
          "exports native.body_velocity",
        ],
        options,
      );
      assert.match(result.stdout, /native.profile_supported: ok/);
      assert.match(result.stdout, /native.body_velocity\(i32, f32, f32, f32\)/);
      const invalid = spawnSync(
        binary,
        ["--self-test", "--command", "api native.gravity 1 2"],
        { ...options, encoding: "utf8", timeout: 30000 },
      );
      assert.notEqual(invalid.status, 0);
      assert.match(invalid.stderr, /Startup mod command failed/);
    });
  },
);
const probe = `
#include <drive_mad_gameplay.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"Gameplay assertion: %s\\n",#x);exit(9);}}while(0)
static const DMApi *a;static DMContext *c;static unsigned ticks,checked,stepped,levels,original,camera_depth;static uint64_t before;
static DMValue get(const char *name) {DMValue r;CHECK(a->call(c,name,NULL,0,&r)==DM_OK);return r;}
static DMValue body(const char *name,uint32_t b) {DMValue r,v=dm_i32(b);CHECK(a->call(c,name,&v,1,&r)==DM_OK);return r;}
static DMStatus one(const char *name,DMValue v) {return a->call(c,name,&v,1,NULL);}
static DMStatus load(const DMApi *api,DMContext *ctx,void **userdata) {
  a=api;c=ctx;CHECK(get("native.profile_supported").as.i32==1);
  CHECK(dm_game_scale(a,c,0.5f)==DM_ENOTREADY);
  DMValue wrong=dm_i32(1);
  CHECK(a->call(c,"native.time_scale",&wrong,1,NULL)==DM_EABI);
  return DM_OK;
}
static void frame(void *u,const DMFrame *f) {
  ticks++;
  if(ticks==20) {
    CHECK(get("native.level_count").as.i32>1);
    uint32_t count=get("native.body_count").as.i32,b=UINT32_MAX;
    CHECK(count>0 && count<=16384);
    for(uint32_t i=0;i<count;i++)if(body("native.body_inverse_mass",i).as.f32>0){b=i;break;}
    CHECK(b!=UINT32_MAX);
    CHECK(isfinite(body("native.body_x",b).as.f32) && isfinite(body("native.body_y",b).as.f32) && isfinite(body("native.body_z",b).as.f32));
    float gx=get("native.gravity_x").as.f32,gy=get("native.gravity_y").as.f32,gz=get("native.gravity_z").as.f32;
    CHECK(dm_game_gravity(a,c,0,-1.62f,0)==DM_OK);
    CHECK(fabsf(get("native.gravity_y").as.f32+1.62f)<0.001f);
    CHECK(dm_call0(a,c,"native.gravity_reset")==DM_OK);
    CHECK(fabsf(get("native.gravity_x").as.f32-gx)<0.001f && fabsf(get("native.gravity_y").as.f32-gy)<0.001f && fabsf(get("native.gravity_z").as.f32-gz)<0.001f);
    CHECK(dm_game_gravity(a,c,NAN,0,0)==DM_EINVAL);
    CHECK(dm_game_gravity(a,c,0,101,0)==DM_EBOUNDS);
    CHECK(dm_game_velocity(a,c,b,1,2,3)==DM_OK);
    CHECK(body("native.body_vx",b).as.f32==1 && body("native.body_vy",b).as.f32==2 && body("native.body_vz",b).as.f32==3);
    CHECK(dm_game_spin(a,c,b,0.5f,1,1.5f)==DM_OK);
    CHECK(body("native.body_wx",b).as.f32==0.5f && body("native.body_wy",b).as.f32==1 && body("native.body_wz",b).as.f32==1.5f);
    CHECK(dm_game_velocity(a,c,UINT32_MAX,0,0,0)==DM_EBOUNDS);
    CHECK(dm_game_velocity(a,c,b,201,0,0)==DM_EBOUNDS);
    uint32_t table=0,pointer=0;
    CHECK(a->memory_read(c,32434048,&table,4)==DM_OK);
    CHECK(a->memory_read(c,table+b*4,&pointer,4)==DM_OK);
    float old_force[3],factor[3],new_force[3];
    CHECK(a->memory_read(c,pointer+436,old_force,12)==DM_OK);
    CHECK(a->memory_read(c,pointer+372,factor,12)==DM_OK);
    CHECK(dm_game_force(a,c,b,1,2,3)==DM_OK);
    CHECK(a->memory_read(c,pointer+436,new_force,12)==DM_OK);
    for(int i=0;i<3;i++)CHECK(fabsf(new_force[i]-(old_force[i]+factor[i]*(i+1)))<0.001f);
    DMValue material[]={dm_i32(b),dm_f32(0.75f)};
    CHECK(a->call(c,"native.body_friction",material,2,NULL)==DM_OK && body("native.body_friction_value",b).as.f32==0.75f);
    CHECK(a->call(c,"native.body_bounce",material,2,NULL)==DM_OK && body("native.body_bounce_value",b).as.f32==0.75f);
    CHECK(dm_call0(a,c,"native.stop_motion")==DM_OK);
    CHECK(body("native.body_vx",b).as.f32==0 && body("native.body_vy",b).as.f32==0 && body("native.body_wz",b).as.f32==0);
    CHECK(one("native.world_lift",dm_f32(3))==DM_OK && body("native.body_vy",b).as.f32==3);
    CHECK(dm_game_scale(a,c,0.5f)==DM_OK && get("native.get_time_scale").as.f32==0.5f);
    CHECK(dm_game_scale(a,c,0)==DM_EBOUNDS);
    CHECK(one("native.auto_drive",dm_i32((uint32_t)-1))==DM_OK && (int32_t)get("native.get_auto_drive").as.i32==-1);
    CHECK(one("native.invert_controls",dm_i32(1))==DM_OK && get("native.get_invert_controls").as.i32==1);
    CHECK(dm_call0(a,c,"native.reset_controls")==DM_OK && get("native.get_time_scale").as.f32==1 && get("native.get_auto_drive").as.i32==0 && get("native.get_invert_controls").as.i32==0);
    checked=1;
  }
  if(ticks==30) {
    DMState s={.struct_size=sizeof(s)};CHECK(a->state(c,&s)==DM_OK);before=s.frames;
    CHECK(dm_game_pause(a,c,1)==DM_OK);
  }
  if(ticks==31)CHECK(dm_call0(a,c,"native.step")==DM_OK);
  if(ticks==33) {
    DMState s={.struct_size=sizeof(s)};CHECK(a->state(c,&s)==DM_OK && s.paused && s.frames==before+1);
    CHECK(dm_game_pause(a,c,0)==DM_OK);stepped=1;
  }
  if(ticks==40) {
    CHECK(a->memory_read(c,38958312,&camera_depth,4)==DM_OK);
    original=get("native.level_index").as.i32;uint32_t count=get("native.level_count").as.i32;
    CHECK(dm_game_level(a,c,count)==DM_EBOUNDS);
    CHECK(dm_game_level(a,c,0)==DM_OK && get("native.level_index").as.i32==0);
    uint32_t depth=0;CHECK(a->memory_read(c,38958312,&depth,4)==DM_OK && depth==camera_depth);
    CHECK(dm_call0(a,c,"native.restart")==DM_EFULL);
  }
  if(ticks==41)CHECK(dm_call0(a,c,"native.restart")==DM_OK);
  if(ticks==42) {
    CHECK(dm_call0(a,c,"native.next_level")==DM_OK && get("native.level_index").as.i32==1);
  }
  if(ticks==43) {
    DMValue v=dm_i32(original);CHECK(a->call(c,"level_select_menu_start_level",&v,1,NULL)==DM_OK);levels=1;
  }
  if(ticks>=41 && ticks<=43) {
    uint32_t depth=0;CHECK(a->memory_read(c,38958312,&depth,4)==DM_OK && depth==camera_depth);
  }
}
static void unload(void *u) {CHECK(checked && stepped && levels);fprintf(stderr,"Native gameplay probe passed: gravity, bodies, force, materials, controls, stepping, levels.\\n");}
DM_PUBLIC const DMMod *dm_mod_query(uint32_t abi) {
  static const DMMod d={.abi_version=1,.struct_size=sizeof(DMMod),.id="gameplay-check",.name="Gameplay check",.version="1.0.0",.on_load=load,.on_frame=frame,.on_unload=unload};
  return abi==1?&d:NULL;
}`;
test(
  "real native game applies researched physics, controls, levels and paused stepping through ABI-1",
  { skip: !gui && "Set DRIVE_MAD_TEST_GUI=1 with a display" },
  async () => {
    await temp(async (dir) => {
      const mod = path.join(dir, "mods/check");
      await mkdir(mod, { recursive: true });
      await writeFile(
        path.join(mod, "mod.ini"),
        "abi=1\nid=gameplay-check\nversion=1.0.0\nlibrary=mod.so\n",
      );
      await writeFile(path.join(mod, "mod.c"), probe);
      run(process.execPath, [
        path.join(root, "scripts/native-mod.mjs"),
        "build",
        mod,
      ]);
      const result = run(
        binary,
        ["--mods", path.join(dir, "mods"), "--smoke-test"],
        {
          cwd: dir,
          env: { ...process.env, DRIVE_MAD_SAVE_DIR: path.join(dir, "save") },
        },
      );
      assert.match(result.stderr, /Native gameplay probe passed/);
      assert.match(result.stderr, /GL_error=0x0/);
    });
  },
);
test(
  "native workbench's real buttons pause, step and resume; all nine tabs render without GL errors",
  { skip: !gui && "Set DRIVE_MAD_TEST_GUI=1 with a display" },
  async () => {
    await temp(async (dir) => {
      const image = path.join(dir, "menu.png");
      const result = run(binary, ["--menu-self-test", "--screenshot", image], {
        cwd: dir,
        env: { ...process.env, DRIVE_MAD_SAVE_DIR: path.join(dir, "save") },
      });
      assert.match(result.stderr, /Native workbench self-test passed/);
      assert.match(result.stderr, /GL_error=0x0/);
      assert.equal((await readFile(image)).subarray(1, 4).toString(), "PNG");
    });
  },
);
