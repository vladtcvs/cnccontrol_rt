#pragma once

#include <stdint.h>

typedef struct {
	uint8_t en:1;
	uint8_t dir:1;
} step_flags;

typedef struct {
	int32_t pos[3];
	int32_t speed[3];
	step_flags flags[3];
	uint8_t abs_crd : 1;
} cnc_position;

void move_line_to(int32_t x[3]);
void find_begin(int rx, int ry, int rz);
void set_speed(int32_t speed);

// step of moving
int32_t step_tick(void);

extern cnc_position position;
extern int busy;

