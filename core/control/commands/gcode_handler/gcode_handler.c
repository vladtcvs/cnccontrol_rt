#include <stdio.h>
#include <stddef.h>
#include <err.h>
#include <gcodes.h>
#include <print_events.h>
#include <print_status.h>
#include <planner.h>
#include <string.h>
#include <unistd.h>

static int handle_g_command(gcode_frame_t *frame)
{
    gcode_cmd_t *cmds = frame->cmds;
    int ncmds = frame->num;
    int nid = -1;

    // skip line number(s)
    while (ncmds > 0 && cmds[0].type == 'N') {
        nid = cmds[0].val_i;
        ncmds--;
        cmds++;
    }

    if (nid == -1)
    {
        send_error(-1, "No command number specified");
        planner_lock();
        return -E_INCORRECT;
    }
    if (ncmds == 0)
    {
        send_ok(nid);
        return -E_OK;
    }
    // parse command line
    switch (cmds[0].type) {
    case 'G':
        switch (cmds[0].val_i) {
        case 0:
        case 1: {
            int i;
            double f = 0, feed0 = 0, feed1 = 0;
            int32_t acc = def.acc_default;
            double x[3] = {0, 0, 0};
            for (i = 1; i < ncmds; i++) {
                switch (cmds[i].type) {
                case 'X':
                    x[0] = cmds[i].val_f/100.0;
                    break;
                case 'Y':
                    x[1] = cmds[i].val_f/100.0;
                    break;
                case 'Z':
                    x[2] = cmds[i].val_f/100.0;
                    break;
                case 'F':
                    f = cmds[i].val_i;
                    break;
                case 'P':
                    feed0 = cmds[i].val_i;
                    break;
                case 'L':
                    feed1 = cmds[i].val_i;
                    break;
                case 'T':
                    acc = cmds[i].val_i;
                    break;
                }
            }
            int res = planner_line_to(x, f, feed0, feed1, acc, nid);
            if (res >= 0)
            {
                return -E_OK;
            }
            else if (res == -E_NOMEM)
            {
                send_error(nid, "no space in buffer");
                planner_lock();
                return res;
            }
            else if (res == -E_LOCKED)
            {
                send_error(nid, "system is locked");
                return res;
            }
            else
            {
                send_error(nid, "problem with planning line");
                planner_lock();
                return res;
            }
            break;
        }
        case 2:
        case 3: {
            int i;
            double f = 0, feed0 = 0, feed1 = 0;
	        int32_t acc = def.acc_default;
            double x[3] = {0, 0, 0};
            int plane = XY;
            double d = 0;
            for (i = 1; i < ncmds; i++) {
                switch (cmds[i].type) {
                case 'X':
                    x[0] = cmds[i].val_f/100.0;
                    break;
                case 'Y':
                    x[1] = cmds[i].val_f/100.0;
                    break;
                case 'Z':
                    x[2] = cmds[i].val_f/100.0;
                    break;
                case 'D':
                    d = cmds[i].val_f/100.0;
                    break;
                case 'F':
                    f = cmds[i].val_i;
                    break;
                case 'P':
                    feed0 = cmds[i].val_i;
                    break;
                case 'L':
                    feed1 = cmds[i].val_i;
                    break;
                case 'T':
                    acc = cmds[i].val_i;
                    break;
                case 'G':
                    switch (cmds[i].val_i)
                    {
                    case 17:
                        plane = XY;
                        break;
                    case 18:
                        plane = YZ;
                        break;
                    case 19:
                        plane = ZX;
                        break;
                    default:
                        break;
                    }
                    break;
                }
            }
            int cw = (cmds[0].val_i == 2);
            int res = planner_arc_to(x, d, plane, cw, f, feed0, feed1, acc, nid);
            if (res >= 0)
            {
                return -E_OK;
            }
            else if (res == -E_NOMEM)
            {
                send_error(nid, "no space in buffer");
                planner_lock();
                return res;
            }
            else if (res == -E_LOCKED)
            {
                send_error(nid, "system is locked");
                return res;
            }
            else
            {
                send_error(nid, "problem with planning arc");
                planner_lock();
                return res;
            }
            break;
        }

        default:
        {
            char buf[60];
            snprintf(buf, 60, "unknown command G%i", cmds[0].val_i);
            send_error(nid, buf);
            planner_lock();
            return -E_INCORRECT;
        }
        }
        break;
    case 'M':
        switch (cmds[0].val_i) {
        case 3:
        case 5:
        {
            int tool = 0;
            int on = (cmds[0].val_i == 3);
	    int i;
            for (i = 1; i < ncmds; i++) {
                switch (cmds[i].type) {
                case 'T':
                    tool = cmds[i].val_i;
		    break;
                }
	    }
            int res = planner_tool(tool, on, nid);
            if (res >= 0)
            {
                return -E_OK;
            }
            else if (res == -E_NOMEM)
            {
                send_error(nid, "no space in buffer");
                planner_lock();
                return res;
            }
            else if (res == -E_LOCKED)
            {
                send_error(nid, "system is locked");
                return res;
            }
            else
            {
                send_error(nid, "problem with planning tool");
                planner_lock();
                return res;
            }

            return -E_OK;
	}
        case 114:
            send_queued(nid);
            print_position(nid);
            return -E_OK;
        case 119:
            send_queued(nid);
            print_endstops(nid);
            return -E_OK;
        case 800:
            planner_unlock();
            send_ok(nid);
            return -E_OK;
        case 801:
            planner_lock();
            send_ok(nid);
            return -E_OK;
        case 802:
            planner_fail_on_endstops(false);
            send_ok(nid);
            return -E_OK;
        case 803:
            planner_fail_on_endstops(true);
            send_ok(nid);
            return -E_OK;
	case 995:
            enable_break_on_probe(false);
            send_ok(nid);
            return -E_OK;
        case 996:
            enable_break_on_probe(true);
            send_ok(nid);
            return -E_OK;
        case 997: {
            double x[3] = {0};
            moves_set_position(x);
            moves_reset();
            send_ok(nid);
            return -E_OK;
        }
        case 998:
            moves_reset();
            send_ok(nid);
            return -E_OK;
        case 999:
            def.reboot();
            // for debug cases
            send_ok(nid);
            return -E_OK;
        default:
        {
            char buf[60];
            snprintf(buf, 60, "unknown command M%i", cmds[0].val_i);
            send_error(nid, buf);
            planner_lock();
            return -E_INCORRECT;
        }
        }
        break;
    default:
    {
        char buf[60];
        snprintf(buf, 60, "unknown command %c%i", cmds[0].type, cmds[0].val_i);
        send_error(nid, buf);
        planner_lock();
        return -E_INCORRECT;
    }
    }
    planner_lock();
    return -E_INCORRECT;
}

int execute_g_command(const unsigned char *command, ssize_t len)
{
    gcode_frame_t frame;
    int rc;

    if (len < 0)
        len = strlen(command);

    rc = parse_cmdline(command, len, &frame);
    switch (rc)
    {
        case -E_CRC:
            send_error(-1, "CRC error");
            return rc;
        case -E_OK:
            return handle_g_command(&frame);
        default:
        {
            planner_lock();
            char buf[60];
            snprintf(buf, 60, "parse error: %.*s", len, command);
            buf[59] = 0;
            send_error(-1, buf);
            return rc;
        }
    }
}
