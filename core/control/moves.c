#include <math/math.h>

#include <shell/print.h>
#include <shell/shell.h>
#include "moves.h"

#define abs(a) ((a) > 0 ? (a) : (-(a))) 

cnc_position position;
int busy;

static int32_t dc[3], dx[3];
static int32_t err[3];
static int maxi;
static int steps;
static int step;
static int step_delay;
static int feed;
static int is_moving;

static steppers_definition def;

void init_moves(steppers_definition definition)
{
	def = definition;
}

static void bresenham_plan(void)
{
	int i;
	steps = abs(dc[0]);
	maxi = 0;
	if (abs(dc[1]) > steps)
	{
		maxi = 1;
		steps = abs(dc[1]);
	}

	if (abs(dc[2]) > steps)
	{
		maxi = 2;
		steps = abs(dc[2]);
	}

	for (i = 0; i < 3; i++)
	{
		def.set_dir(i, dc[i] >= 0);
	}
	step = 0;
}

// len is measured in 0.01 mm
// feed in mm / min
// delay in usec
uint32_t feed_to_delay(uint32_t feed, uint32_t len, uint32_t steps)
{
	uint32_t k = 100 * len / steps;
	if (feed == 0)
		feed = 1;

	//return 1000000 * len * 60  / (feed * 100 * steps);
	return k * 60 * 100 / feed;
}

void move_line_to(int32_t x[3])
{
	uint32_t len = 0;
	int32_t t;
    	int i;
    	for (i = 0; i < 3; i++)
    	{
	    	dx[i] = x[i];
		dc[i] = x[i] * def.steps_per_unit[i] / 100;
    	}
	
	bresenham_plan();
    	if (steps == 0)
		return;

	len = abs(dx[maxi]);

	step_delay = feed_to_delay(feed, len, steps);

/*	
	shell_send_string("feed: ");
	shell_print_dec(feed);
	shell_send_string("\r\nsteps: ");
	shell_print_dec(steps);
	shell_send_string("\r\nlen: ");
	shell_print_dec(len);
	shell_send_string("\r\ndelay: ");
	shell_print_dec(step_delay);
	shell_send_string("\n\r");*/
	is_moving = 1;
	def.line_started();
}

int step_tick(void)
{
	int i;
	if (step >= steps)
		return -1;

	def.make_step(maxi);
	for (i = 0; i < 3; i++) {
		if (i == maxi)
			continue;
		err[i] += abs(dc[i]);
		if (err[i] * 2 >= steps) {
			err[i] -= steps;
			def.make_step(i);
		}
	}

	step++;
	if (step == steps) {
		for (i = 0; i < 3; i++)
			position.pos[i] += dx[i];
        	def.line_finished();
        	return -1;
    	}
    	return step_delay;
}

void set_speed(int32_t speed)
{
	feed = speed;
}

void find_begin(int rx, int ry, int rz)
{

}

cnc_endstops moves_get_endstops(void)
{
	return def.get_endstops();
}

