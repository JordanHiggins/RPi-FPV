#pragma once

typedef struct cam *cam_t;

void cam_deinit(cam_t cam);
const char *cam_error(void);
cam_t cam_init(void);

int cam_recording(cam_t cam);
int cam_start(cam_t cam, const char *path);
int cam_stop(cam_t cam);
