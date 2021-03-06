#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <control/moves/moves.h>
#include <control/tools/tools.h>
#include <control/planner/planner.h>
#include <err/err.h>

#ifndef QUEUE_SIZE
#define QUEUE_SIZE 10
#endif

extern cnc_position position;

static int last_nid;
static volatile int search_begin;

static volatile bool locked = true;
static volatile bool fail_on_endstops = true;

static void (*finish_action)(void);
static void _planner_lock(void);

typedef enum {
    ACTION_NONE = 0,
    ACTION_LINE,
    ACTION_ARC,
    ACTION_TOOL,
} action_type;

typedef enum {
    STATE_NONE = 0,
    STATE_QUEUED,
    STATE_PREPARED,
    STATE_STARTED,
    STATE_FINISHED,
    STATE_FAILED,
} action_state;

typedef struct {
    int nid;
    action_state state;
    bool state_changed;
    action_type type;
    union {
        line_plan line;
        arc_plan arc;
        tool_plan tool;
    };
} action_plan;

static action_plan plan[QUEUE_SIZE];
static int plan_first = 0;
static int plan_cur = 0;
static int plan_last = 0;

static int active_plan_len = 0;
static int plan_len = 0;

static bool break_on_probe = false;

static void (*ev_send_started)(int nid);
static void (*ev_send_completed)(int nid);
static void (*ev_send_completed_with_pos)(int nid, const int32_t *pos);
static void (*ev_send_queued)(int nid);
static void (*ev_send_dropped)(int nid);
static void (*ev_send_failed)(int nid);

static void next_cmd(void)
{
    if (active_plan_len > 0)
    {
        plan_cur = (plan_cur + 1) % QUEUE_SIZE;
	active_plan_len--;
    }
}

static void pop_cmd(void)
{
    if (plan_len > 0)
    {
        memset(&plan[plan_first], 0, sizeof(action_plan));
        plan[plan_first].state = STATE_NONE;

        plan_first = (plan_first + 1) % QUEUE_SIZE;
        plan_len--;
    }
}

void planner_report_states(void)
{
    int id;
    for (id = 0; id < QUEUE_SIZE; id++)
    {
        action_plan *p = &plan[id];
	if (p->state == STATE_NONE)
            continue;
        if (!p->state_changed)
            continue;
	switch (p->state)
        {
        case STATE_STARTED:
            ev_send_started(p->nid);
            break;
        case STATE_FAILED:
            ev_send_failed(p->nid);
            pop_cmd();
            break;
        case STATE_FINISHED:
            ev_send_completed(p->nid);
            pop_cmd();
            break;
        }
        p->state_changed = false;
    }
}

static void get_cmd(void)
{
    if (active_plan_len == 0)
    {
    	return;
    }

    action_plan *cp = &plan[plan_cur];
    int res;

    cp->state = STATE_STARTED;
    cp->state_changed = true;

    switch (cp->type) {
    case ACTION_LINE:
        res = moves_line_to(&(cp->line));
        if (res == -E_NEXT)
        {
            cp->state = STATE_FINISHED;
            cp->state_changed = true;
            next_cmd();
            get_cmd();
        }
        break;
    case ACTION_ARC:
        res = moves_arc_to(&(cp->arc));
        if (res == -E_NEXT)
        {
            cp->state = STATE_FINISHED;
            cp->state_changed = true;
            next_cmd();
            get_cmd();
        }
        break;
    case ACTION_TOOL:
        res = tool_action(&(cp->tool));
        if (res == -E_NEXT)
        {
            cp->state = STATE_FINISHED;
            cp->state_changed = true;
            next_cmd();
            get_cmd();
        }
        break;
    case ACTION_NONE:
        cp->state = STATE_FINISHED;
        cp->state_changed = true;
        next_cmd();
        get_cmd();
        break;
    }
}

static void (*line_started_cb)(void);
static void (*line_finished_cb)(void);
static void (*line_error_cb)(void);

static void line_started(void)
{
    if (locked)
        return;
    line_started_cb();
}

static void line_finished(void)
{
    action_plan *cp = &plan[plan_cur];
    cp->state = STATE_FINISHED;
    cp->state_changed = true;
    line_finished_cb();
    next_cmd();

    if (locked)
        return;

    get_cmd();
}

static void endstops_touched(void)
{
    if (!fail_on_endstops)
    {
        line_finished();
    }
    else
    {
        action_plan *cp = &plan[plan_cur];
	_planner_lock();
	cp->state = STATE_FAILED;
	cp->state_changed = true;
        line_error_cb();
        next_cmd();
    }
}

static steppers_definition steppers_definitions;
static gpio_definition gpio_definitions;

void init_planner(steppers_definition *def,
		  gpio_definition *gd,
                  void (*arg_send_queued)(int nid),
                  void (*arg_send_started)(int nid),
                  void (*arg_send_completed)(int nid),
                  void (*arg_send_completed_with_pos)(int nid, const int32_t *pos),
                  void (*arg_send_dropped)(int nid),
                  void (*arg_send_failed)(int nid))
{
    ev_send_started = arg_send_started;
    ev_send_completed = arg_send_completed;
    ev_send_completed_with_pos = arg_send_completed_with_pos;
    ev_send_queued = arg_send_queued;
    ev_send_dropped = arg_send_dropped;
    ev_send_failed = arg_send_failed;
    plan_cur = plan_last = 0;
    plan_len = 0;
    active_plan_len = 0;
    search_begin = 0;
    finish_action = NULL;

    memcpy(&steppers_definitions, def, sizeof(*def));
    memcpy(&gpio_definitions, gd, sizeof(*gd));

    line_started_cb = def->line_started;
    line_finished_cb = def->line_finished;
    line_error_cb = def->line_error;

    steppers_definitions.line_started = line_started;
    steppers_definitions.endstops_touched = endstops_touched;
    steppers_definitions.line_finished = line_finished;

    moves_init(&steppers_definitions);
    tools_init(&gpio_definitions);
}

int active_slots(void)
{
    return active_plan_len;
}

int used_slots(void)
{
    return plan_len;
}

int empty_slots(void)
{
    return QUEUE_SIZE - used_slots() - 1;
}

static int break_on_endstops(int32_t *dx, void *user_data)
{
    cnc_endstops endstops = steppers_definitions.get_endstops();
    if (endstops.stop_x && dx[0] < 0)
        return 1;

    if (endstops.stop_y && dx[1] < 0)
        return 1;

    if (endstops.stop_z && dx[2] < 0)
        return 1;

    if (break_on_probe && endstops.probe && dx[2] >= 0)
        return 1;

    return 0;
}

static int _planner_line_to(int32_t x[3], int (*cbr)(int32_t *, void *), void *usr_data,
                            double feed, double f0, double f1, int32_t acc, int nid)
{
    action_plan *cur;

    if (x[0] == 0 && x[1] == 0 && x[2] == 0)
        return 0;

    if (f0 < steppers_definitions.feed_base)
        f0 = steppers_definitions.feed_base;

    if (f1 < steppers_definitions.feed_base)
        f1 = steppers_definitions.feed_base;

    if (feed < steppers_definitions.feed_base)
        feed = steppers_definitions.feed_base;

    cur = &plan[plan_last];
    cur->type = ACTION_LINE;
    cur->nid = nid;
    if (cbr != NULL)
    {
        cur->line.check_break = cbr;
        cur->line.check_break_data = usr_data;
    }
    else
    {
        cur->line.check_break = break_on_endstops;
        cur->line.check_break_data = x;
    }
    cur->line.x[0] = x[0];
    cur->line.x[1] = x[1];
    cur->line.x[2] = x[2];
    cur->line.feed = feed;
    cur->line.feed0 = f0;
    cur->line.feed1 = f1;
    cur->line.acceleration = acc;
    cur->line.len = -1;
    cur->line.acc_steps = -1;
    cur->line.dec_steps = -1;

    plan_last = (plan_last + 1) % QUEUE_SIZE;
    plan_len++;
    active_plan_len++;

    cur->state = STATE_QUEUED;
    cur->state_changed = false;
    return 1;
}

int planner_line_to(int32_t x[3], double feed, double f0, double f1, int32_t acc, int nid)
{
    if (planner_is_locked())
    {
        return -E_LOCKED;
    }

    if (empty_slots() == 0)
    {
        return -E_NOMEM;
    }

    int res = _planner_line_to(x, NULL, NULL, feed, f0, f1, acc, nid);
    if (res)
    {
        ev_send_queued(nid);
        if (active_slots() == 1) {
            get_cmd();
        }
    }
    else
    {
        ev_send_dropped(nid);
    }
    
    last_nid = nid;
    return empty_slots();
}

static int _planner_arc_to(int32_t x1[2], int32_t x2[2], int32_t H, double len, double a, double b, arc_plane plane, int cw,
			   int (*cbr)(int32_t *, void *), void *usr_data,
                           double feed, double f0, double f1, int32_t acc, int nid)
{
    action_plan *cur;

    if (x1[0] == x2[0] && x1[1] == x2[1])
        return 0;

    if (f0 < steppers_definitions.feed_base)
        f0 = steppers_definitions.feed_base;

    if (f1 < steppers_definitions.feed_base)
        f1 = steppers_definitions.feed_base;

    if (feed < steppers_definitions.feed_base)
        feed = steppers_definitions.feed_base;

    cur = &plan[plan_last];
    cur->type = ACTION_ARC;
    cur->nid = nid;
    if (cbr != NULL)
    {
        cur->arc.check_break = cbr;
        cur->arc.check_break_data = usr_data;
    }
    else
    {
        cur->arc.check_break = break_on_endstops;
        cur->arc.check_break_data = NULL;
    }
    cur->arc.H = H;
    cur->arc.x1[0] = x1[0];
    cur->arc.x1[1] = x1[1];
    cur->arc.x2[0] = x2[0];
    cur->arc.x2[1] = x2[1];
    cur->arc.len = len;

    cur->arc.a = a;
    cur->arc.b = b;
    cur->arc.cw = cw;
    cur->arc.plane = plane;
    cur->arc.feed = feed;
    cur->arc.feed0 = f0;
    cur->arc.feed1 = f1;
    cur->arc.acceleration = acc;
    cur->arc.ready = 0;

    plan_last = (plan_last + 1) % QUEUE_SIZE;
    plan_len++;
    active_plan_len++;

    cur->state = STATE_QUEUED;
    cur->state_changed = false;
    return 1;
}


int planner_arc_to(int32_t x1[2], int32_t x2[2], int32_t H, double len, double a, double b, arc_plane plane, int cw,
		   double feed, double f0, double f1, int32_t acc, int nid)
{
    if (planner_is_locked())
    {
        return -E_LOCKED;
    }

    if (empty_slots() == 0)
    {
        return -E_NOMEM;
    }

    int res = _planner_arc_to(x1, x2, H, len, a, b, plane, cw, NULL, NULL, feed, f0, f1, acc, nid);
    if (res)
    {
        ev_send_queued(nid);
        if (active_slots() == 1) {
            get_cmd();
        }
    }
    else
    {
        ev_send_dropped(nid);
    }
    
    last_nid = nid;
    return empty_slots();
}

int planner_tool(int id, bool on, int nid)
{
    if (planner_is_locked())
    {
        return -E_LOCKED;
    }

    if (empty_slots() == 0)
    {
        return -E_NOMEM;
    }

    action_plan *cur;

    cur = &plan[plan_last];
    cur->type = ACTION_TOOL;
    cur->nid = nid;

    cur->tool.on = on;
    cur->tool.id = id;

    plan_last = (plan_last + 1) % QUEUE_SIZE;
    plan_len++;
    active_plan_len++;

    ev_send_queued(nid);
    last_nid = nid;

    cur->state = STATE_QUEUED;
    cur->state_changed = false;

    if (active_slots() == 1) {
        get_cmd();
    }
    return empty_slots();
}

static int srx, sry, srz;

void enable_break_on_probe(bool en)
{
    break_on_probe = en;
}

void planner_pre_calculate(void)
{
    int i;
    for (i = 0; i < active_slots(); i++)
    {
        int pos = (plan_cur + i) % QUEUE_SIZE;
        action_plan *p = &plan[pos];
        switch(p->type)
        {
            case ACTION_LINE:
                if (p->state == STATE_QUEUED)
                {
                    line_pre_calculate(&(p->line));
		    p->state = STATE_PREPARED;
		    p->state_changed = true;
                }
                break;
            case ACTION_ARC:
                if (p->state == STATE_QUEUED)
                {
                    arc_pre_calculate(&(p->arc));
		    p->state = STATE_PREPARED;
		    p->state_changed = true;
                }
                break;
            default:
                break;
        }
    }
}

static void _planner_lock(void)
{
    locked = 1;
    plan_cur = plan_last = 0;
    plan_len = 0;
    active_plan_len = 0;
    moves_break();
}

void planner_lock(void)
{
    _planner_lock();
    steppers_definitions.line_finished();
}

void planner_unlock(void)
{
    if (!moves_common_def.configured)
    {
        return;
    }
    locked = 0;
}

int planner_is_locked(void)
{
    return locked;
}

void planner_fail_on_endstops(bool fail)
{
    fail_on_endstops = fail;
}

