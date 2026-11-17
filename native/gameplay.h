#ifndef DRIVE_MAD_HOST_GAMEPLAY_H
#define DRIVE_MAD_HOST_GAMEPLAY_H
typedef struct {
  float speed, gravity[3], gravity_original[3];
  bool gravity_enabled, invert, clock_started, level_changed;
  int auto_drive;
  unsigned steps;
  double clock_ms;
  uint64_t clock_real, level_frame;
  u32 scratch;
} Gameplay;
#endif
