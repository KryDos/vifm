/* vifm
 * Copyright (C) 2001 Ken Steen.
 * Copyright (C) 2011 xaizek.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "modes.h"

#include <curses.h>

#include <stddef.h> /* wchar_t */

#include "../engine/keys.h"
#include "../engine/mode.h"
#include "../utils/log.h"
#include "../utils/macros.h"
#include "../main_loop.h"
#include "../status.h"
#include "../ui.h"
#include "dialogs/attr_dialog.h"
#include "dialogs/change_dialog.h"
#include "dialogs/sort_dialog.h"
#include "cmdline.h"
#include "file_info.h"
#include "menu.h"
#include "normal.h"
#include "view.h"
#include "visual.h"

static int mode_flags[] = {
	MF_USES_COUNT | MF_USES_REGS, /* NORMAL_MODE */
	MF_USES_INPUT,                /* CMDLINE_MODE */
	MF_USES_COUNT | MF_USES_REGS, /* VISUAL_MODE */
	MF_USES_COUNT,                /* MENU_MODE */
	MF_USES_COUNT,                /* SORT_MODE */
	MF_USES_COUNT,                /* ATTR_MODE */
	MF_USES_COUNT,                /* CHANGE_MODE */
	MF_USES_COUNT,                /* VIEW_MODE */
	0,                            /* FILE_INFO_MODE */
};
ARRAY_GUARD(mode_flags, MODES_COUNT);

static char uses_input_bar[] = {
	1, /* NORMAL_MODE */
	0, /* CMDLINE_MODE */
	1, /* VISUAL_MODE */
	1, /* MENU_MODE */
	1, /* SORT_MODE */
	1, /* ATTR_MODE */
	1, /* CHANGE_MODE */
	1, /* VIEW_MODE */
	1, /* FILE_INFO_MODE */
};
ARRAY_GUARD(uses_input_bar, MODES_COUNT);

typedef void (*mode_init_func)(void);
static mode_init_func mode_init_funcs[] = {
	&init_normal_mode,        /* NORMAL_MODE */
	&init_cmdline_mode,       /* CMDLINE_MODE */
	&init_visual_mode,        /* VISUAL_MODE */
	&init_menu_mode,          /* MENU_MODE */
	&init_sort_dialog_mode,   /* SORT_MODE */
	&init_attr_dialog_mode,   /* ATTR_MODE */
	&init_change_dialog_mode, /* CHANGE_MODE */
	&init_view_mode,          /* VIEW_MODE */
	&init_file_info_mode,     /* FILE_INFO_MODE */
};
ARRAY_GUARD(mode_init_funcs, MODES_COUNT);

static void modes_statusbar_update(void);
static void update_vmode_input(void);

void
init_modes(void)
{
	LOG_FUNC_ENTER;

	int i;

	init_keys(MODES_COUNT, (int*)&mode_flags);

	for(i = 0; i < MODES_COUNT; ++i)
	{
		mode_init_funcs[i]();
	}
}

void
modes_pre(void)
{
	if(vle_mode_is(CMDLINE_MODE))
	{
		touchwin(status_bar);
		wrefresh(status_bar);
		return;
	}
	else if(ANY(vle_mode_is, SORT_MODE, CHANGE_MODE, ATTR_MODE))
	{
		return;
	}
	else if(vle_mode_is(VIEW_MODE))
	{
		view_pre();
		return;
	}
	else if(is_in_menu_like_mode())
	{
		menu_pre();
		return;
	}

	if(!curr_stats.save_msg)
	{
		clean_status_bar();
		wrefresh(status_bar);
	}
}

void
modes_post(void)
{
	if(ANY(vle_mode_is, CMDLINE_MODE, SORT_MODE, CHANGE_MODE, ATTR_MODE))
	{
		return;
	}
	else if(vle_mode_is(VIEW_MODE))
	{
		view_post();
		return;
	}
	else if(is_in_menu_like_mode())
	{
		menu_post();
		return;
	}

	update_screen(curr_stats.need_update);

	if(curr_stats.save_msg)
	{
		status_bar_message(NULL);
	}

	if(!vle_mode_is(FILE_INFO_MODE) && curr_view->list_rows > 0)
	{
		if(!is_status_bar_multiline())
		{
			update_stat_window(curr_view);
			update_pos_window(curr_view);
		}
	}

	modes_statusbar_update();
}

void
modes_statusbar_update(void)
{
	if(curr_stats.save_msg)
	{
		if(vle_mode_is(VISUAL_MODE))
		{
			update_vmode_input();
		}
	}
	else if(curr_view->selected_files || vle_mode_is(VISUAL_MODE))
	{
		print_selected_msg();
	}
	else
	{
		clean_status_bar();
	}
}

void
modes_redraw(void)
{
	LOG_FUNC_ENTER;

	static int in_here;

	if(curr_stats.load_stage < 2)
	{
		return;
	}

	if(in_here++ > 0)
	{
		/* TODO: is this still needed?  Update scheduling might have solved issues
		 * caused by asynchronous execution of this function in the past. */
		return;
	}

	if(curr_stats.too_small_term)
	{
		update_screen(UT_REDRAW);
		goto finish;
	}

	if(vle_mode_is(CMDLINE_MODE))
	{
		redraw_cmdline();
		goto finish;
	}
	else if(vle_mode_is(MENU_MODE))
	{
		menu_redraw();
		goto finish;
	}
	else if(vle_mode_is(FILE_INFO_MODE))
	{
		redraw_file_info_dialog();
		goto finish;
	}

	update_screen(UT_REDRAW);

	if(curr_stats.save_msg)
	{
		status_bar_message(NULL);
	}

	if(vle_mode_is(SORT_MODE))
	{
		redraw_sort_dialog();
	}
	else if(vle_mode_is(CHANGE_MODE))
	{
		redraw_change_dialog();
	}
	else if(vle_mode_is(ATTR_MODE))
	{
		redraw_attr_dialog();
	}
	else if(vle_mode_is(VIEW_MODE))
	{
		view_redraw();
	}

finish:
	if(--in_here > 0)
	{
		modes_redraw();
	}
}

void
modes_update(void)
{
	if(vle_mode_is(CMDLINE_MODE))
	{
		redraw_cmdline();
		return;
	}
	else if(vle_mode_is(MENU_MODE))
	{
		menu_redraw();
		return;
	}
	else if(vle_mode_is(FILE_INFO_MODE))
	{
		redraw_file_info_dialog();
		return;
	}

	touchwin(stdscr);
	update_all_windows();

	if(vle_mode_is(SORT_MODE))
	{
		redraw_sort_dialog();
	}
	else if(vle_mode_is(CHANGE_MODE))
	{
		redraw_change_dialog();
	}
	else if(vle_mode_is(ATTR_MODE))
	{
		redraw_attr_dialog();
	}
}

void
modupd_input_bar(wchar_t *str)
{
	if(vle_mode_is(VISUAL_MODE))
	{
		clear_input_bar();
	}

	if(uses_input_bar[vle_mode_get()])
	{
		update_input_bar(str);
	}
}

void
clear_input_bar(void)
{
	if(uses_input_bar[vle_mode_get()] && !vle_mode_is(VISUAL_MODE))
	{
		clear_num_window();
	}
}

int
is_in_menu_like_mode(void)
{
	return ANY(vle_mode_is, MENU_MODE, FILE_INFO_MODE);
}

void
print_selected_msg(void)
{
	if(vle_mode_is(VISUAL_MODE))
	{
		status_bar_messagef("-- %s -- ", describe_visual_mode());
		update_vmode_input();
	}
	else
	{
		status_bar_messagef("%d %s selected", curr_view->selected_files,
				curr_view->selected_files == 1 ? "file" : "files");
	}
	curr_stats.save_msg = 2;
}

static void
update_vmode_input(void)
{
	if(is_input_buf_empty())
	{
		werase(input_win);
		checked_wmove(input_win, 0, 0);
		wprintw(input_win, "%d", curr_view->selected_files);
		wrefresh(input_win);
	}
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
