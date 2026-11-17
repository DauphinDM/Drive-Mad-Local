/* Optional gameplay conveniences over ABI 1's dynamically enumerated calls.
 * Availability is a runtime property. See GAMEPLAY.md for limits and units. */
#ifndef DRIVE_MAD_GAMEPLAY_H
#define DRIVE_MAD_GAMEPLAY_H
#include "drive_mad.h"
static inline DMStatus dm_game_scale(const DMApi *a, DMContext *c,
                                     float speed) {
  DMValue v = dm_f32(speed);
  return a->call(c, "native.time_scale", &v, 1, NULL);
}
static inline DMStatus dm_game_pause(const DMApi *a, DMContext *c, int paused) {
  DMValue v = dm_i32((uint32_t)paused);
  return a->call(c, "native.pause", &v, 1, NULL);
}
static inline DMStatus dm_game_level(const DMApi *a, DMContext *c,
                                     uint32_t level) {
  DMValue v = dm_i32(level);
  return a->call(c, "native.start_level", &v, 1, NULL);
}
static inline DMStatus dm_game_gravity(const DMApi *a, DMContext *c, float x,
                                       float y, float z) {
  DMValue v[] = {dm_f32(x), dm_f32(y), dm_f32(z)};
  return a->call(c, "native.gravity", v, 3, NULL);
}
static inline DMStatus dm_game_body_vector(const DMApi *a, DMContext *c,
                                           const char *name, uint32_t body,
                                           float x, float y, float z) {
  DMValue v[] = {dm_i32(body), dm_f32(x), dm_f32(y), dm_f32(z)};
  return a->call(c, name, v, 4, NULL);
}
static inline DMStatus dm_game_velocity(const DMApi *a, DMContext *c,
                                        uint32_t body, float x, float y,
                                        float z) {
  return dm_game_body_vector(a, c, "native.body_velocity", body, x, y, z);
}
static inline DMStatus dm_game_spin(const DMApi *a, DMContext *c, uint32_t body,
                                    float x, float y, float z) {
  return dm_game_body_vector(a, c, "native.body_spin", body, x, y, z);
}
static inline DMStatus dm_game_force(const DMApi *a, DMContext *c,
                                     uint32_t body, float x, float y, float z) {
  return dm_game_body_vector(a, c, "native.body_force", body, x, y, z);
}
#endif
