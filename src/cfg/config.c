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

#include "config.h"

#define HOME_EV "HOME"
#define VIFM_EV "VIFM"
#define MYVIFMRC_EV "MYVIFMRC"
#define TRASH "Trash"
#define LOG "log"
#define VIFMRC "vifmrc"

#ifndef _WIN32
#define CP_HELP "cp " PACKAGE_DATA_DIR "/" VIFM_HELP " ~/.vifm"
#define CP_RC "cp " PACKAGE_DATA_DIR "/" VIFMRC " ~/.vifm"
#endif

#include <assert.h> /* assert() */
#include <limits.h> /* INT_MIN */
#include <stddef.h> /* size_t */
#include <stdio.h> /* FILE snprintf() */
#include <stdlib.h>
#include <string.h> /* memmove() memset() strdup() */

#include "../menus/menus.h"
#include "../utils/env.h"
#include "../utils/file_streams.h"
#include "../utils/fs.h"
#include "../utils/fs_limits.h"
#include "../utils/log.h"
#include "../utils/macros.h"
#include "../utils/str.h"
#include "../utils/path.h"
#include "../utils/utils.h"
#include "../bookmarks.h"
#include "../commands.h"
#include "../filelist.h"
#include "../opt_handlers.h"
#include "../status.h"
#include "../types.h"
#include "../ui.h"
#include "hist.h"

/* Maximum supported by the implementation length of line in vifmrc file. */
#define MAX_VIFMRC_LINE_LEN 4*1024

/* Default value of shell command used if $SHELL environment variable isn't
 * set. */
#ifndef _WIN32
#define DEFAULT_SHELL_CMD "sh"
#else
#define DEFAULT_SHELL_CMD "cmd"
#endif

/* Default value of the cd path list. */
#define DEFAULT_CD_PATH ""

config_t cfg;

static void find_home_dir(void);
static int try_home_envvar_for_home(void);
static int try_userprofile_envvar_for_home(void);
static int try_homepath_envvar_for_home(void);
static void find_config_dir(void);
static int try_vifm_envvar_for_conf(void);
static int try_exe_directory_for_conf(void);
static int try_home_envvar_for_conf(void);
static int try_appdata_for_conf(void);
static void find_config_file(void);
static int try_myvifmrc_envvar_for_vifmrc(void);
static int try_exe_directory_for_vifmrc(void);
static int try_vifm_vifmrc_for_vifmrc(void);
static void store_config_paths(void);
static void create_config_dir(void);
#ifndef _WIN32
static void create_help_file(void);
static void create_rc_file(void);
#endif
static void add_default_bookmarks(void);
static int source_file_internal(FILE *fp, const char filename[]);
static void show_sourcing_error(const char filename[], int line_num);
static const char * get_tmpdir(void);
static int is_conf_file(const char file[]);
static void disable_history(void);
static void free_view_history(FileView *view);
static void decrease_history(size_t new_len, size_t removed_count);
static void reduce_view_history(FileView *view, size_t size);
static void reallocate_history(size_t new_len);
static void zero_new_history_items(size_t old_len, size_t delta);
static void save_into_history(const char item[], hist_t *hist, int len);

void
init_config(void)
{
	cfg.show_one_window = 0;
	cfg.history_len = 15;

	(void)hist_init(&cfg.cmd_hist, cfg.history_len);
	(void)hist_init(&cfg.search_hist, cfg.history_len);
	(void)hist_init(&cfg.prompt_hist, cfg.history_len);
	(void)hist_init(&cfg.filter_hist, cfg.history_len);

	cfg.auto_execute = 0;
	cfg.time_format = strdup(" %m/%d %H:%M");
	cfg.wrap_quick_view = 1;
	cfg.use_iec_prefixes = 0;
	cfg.undo_levels = 100;
	cfg.sort_numbers = 0;
	cfg.follow_links = 1;
	cfg.fast_run = 0;
	cfg.confirm = 1;
	cfg.vi_command = strdup("vim");
	cfg.vi_cmd_bg = 0;
	cfg.vi_x_command = strdup("");
	cfg.vi_x_cmd_bg = 0;
	cfg.use_trash = 1;

	{
		char fuse_home[PATH_MAX];
		int update_stat;
		snprintf(fuse_home, sizeof(fuse_home), "%s/vifm_FUSE", get_tmpdir());
		update_stat = set_fuse_home(fuse_home);
		assert(update_stat == 0);
	}

	cfg.use_term_multiplexer = 0;
	cfg.use_vim_help = 0;
	cfg.wild_menu = 0;
	cfg.ignore_case = 0;
	cfg.smart_case = 0;
	cfg.hl_search = 1;
	cfg.vifm_info = VIFMINFO_BOOKMARKS;
	cfg.auto_ch_pos = 1;
	cfg.timeout_len = 1000;
	cfg.scroll_off = 0;
	cfg.gdefault = 0;
#ifndef _WIN32
	cfg.slow_fs_list = strdup("");
#endif
	cfg.scroll_bind = 0;
	cfg.wrap_scan = 1;
	cfg.inc_search = 0;
	cfg.selection_is_primary = 1;
	cfg.tab_switches_pane = 1;
	cfg.use_system_calls = 0;
	cfg.last_status = 1;
	cfg.tab_stop = 8;
	cfg.ruler_format = strdup("%=%l/%S ");
	cfg.status_line = strdup("");

	cfg.lines = INT_MIN;
	cfg.columns = INT_MIN;

	cfg.dot_dirs = DD_NONROOT_PARENT;

	cfg.trunc_normal_sb_msgs = 0;

	cfg.filter_inverted_by_default = 1;

	cfg.apropos_prg = strdup("apropos %a");
	cfg.find_prg = strdup("find %s %a -print , "
			"-type d \\( ! -readable -o ! -executable \\) -prune");
	cfg.grep_prg = strdup("grep -n -H -I -r %i %a %s");
	cfg.locate_prg = strdup("locate %a");

	cfg.cd_path = strdup(env_get_def("CDPATH", DEFAULT_CD_PATH));
	replace_char(cfg.cd_path, ':', ',');

	cfg.filelist_col_padding = 1;
	cfg.side_borders_visible = 1;

	cfg.border_filler = strdup(" ");

#ifndef _WIN32
	copy_str(cfg.log_file, sizeof(cfg.log_file), "/var/log/vifm-startup-log");
#else
	{
		char exe_dir[PATH_MAX];
		(void)get_exe_dir(exe_dir, sizeof(exe_dir));
		snprintf(cfg.log_file, sizeof(cfg.log_file), "%s/startup-log", exe_dir);
	}
#endif

	cfg_set_shell(env_get_def("SHELL", DEFAULT_SHELL_CMD));

	memset(&cfg.decorations, '\0', sizeof(cfg.decorations));
	cfg.decorations[DIRECTORY][DECORATION_SUFFIX] = '/';
}

/* searches for configuration file and directories, stores them and ensures
 * existence of some of them */
void
set_config_paths(void)
{
	LOG_FUNC_ENTER;

	find_home_dir();
	find_config_dir();
	find_config_file();

	store_config_paths();

	create_config_dir();
}

/* tries to find home directory */
static void
find_home_dir(void)
{
	LOG_FUNC_ENTER;

	if(try_home_envvar_for_home())
		return;
	if(try_userprofile_envvar_for_home())
		return;
	if(try_homepath_envvar_for_home())
		return;
}

/* tries to use HOME environment variable to find home directory */
static int
try_home_envvar_for_home(void)
{
	LOG_FUNC_ENTER;

	const char *home = env_get(HOME_EV);
	return home != NULL && is_dir(home);
}

/* tries to use USERPROFILE environment variable to find home directory */
static int
try_userprofile_envvar_for_home(void)
{
	LOG_FUNC_ENTER;

#ifndef _WIN32
	return 0;
#else
	char home[PATH_MAX];
	const char *userprofile = env_get("USERPROFILE");
	if(userprofile == NULL || !is_dir(userprofile))
		return 0;
	snprintf(home, sizeof(home), "%s", userprofile);
	to_forward_slash(home);
	env_set(HOME_EV, home);
	return 1;
#endif
}

static int
try_homepath_envvar_for_home(void)
{
	LOG_FUNC_ENTER;

#ifndef _WIN32
	return 0;
#else
	char home[PATH_MAX];
	const char *homedrive = env_get("HOMEDRIVE");
	const char *homepath = env_get("HOMEPATH");
	if(homedrive == NULL || !is_dir(homedrive))
		return 0;
	if(homepath == NULL || !is_dir(homepath))
		return 0;

	snprintf(home, sizeof(home), "%s%s", homedrive, homepath);
	to_forward_slash(home);
	env_set(HOME_EV, home);
	return 1;
#endif
}

/* tries to find configuration directory */
static void
find_config_dir(void)
{
	LOG_FUNC_ENTER;

	if(try_vifm_envvar_for_conf())
		return;
	if(try_exe_directory_for_conf())
		return;
	if(try_home_envvar_for_conf())
		return;
	if(try_appdata_for_conf())
		return;
}

/* tries to use VIFM environment variable to find configuration directory */
static int
try_vifm_envvar_for_conf(void)
{
	LOG_FUNC_ENTER;

	const char *vifm = env_get(VIFM_EV);
	return vifm != NULL && is_dir(vifm);
}

/* tries to use directory of executable file as configuration directory */
static int
try_exe_directory_for_conf(void)
{
	LOG_FUNC_ENTER;

	char exe_dir[PATH_MAX];

	if(get_exe_dir(exe_dir, sizeof(exe_dir)) != 0)
	{
		return 0;
	}

	if(!path_exists_at(exe_dir, VIFMRC))
	{
		return 0;
	}

	env_set(VIFM_EV, exe_dir);
	return 1;
}

/* tries to use $HOME/.vifm as configuration directory */
static int
try_home_envvar_for_conf(void)
{
	LOG_FUNC_ENTER;

	char vifm[PATH_MAX];
	const char *home = env_get(HOME_EV);
	if(home == NULL || !is_dir(home))
		return 0;
	snprintf(vifm, sizeof(vifm), "%s/.vifm", home);
#ifdef _WIN32
	if(!is_dir(vifm))
		return 0;
#endif
	env_set(VIFM_EV, vifm);
	return 1;
}

/* tries to use $APPDATA/Vifm as configuration directory */
static int
try_appdata_for_conf(void)
{
	LOG_FUNC_ENTER;

#ifndef _WIN32
	return 0;
#else
	char vifm[PATH_MAX];
	const char *appdata = env_get("APPDATA");
	if(appdata == NULL || !is_dir(appdata))
		return 0;
	snprintf(vifm, sizeof(vifm), "%s/Vifm", appdata);
	to_forward_slash(vifm);
	env_set(VIFM_EV, vifm);
	return 1;
#endif
}

/* tries to find configuration file */
static void
find_config_file(void)
{
	LOG_FUNC_ENTER;

	if(try_myvifmrc_envvar_for_vifmrc())
		return;
	if(try_exe_directory_for_vifmrc())
		return;
	if(try_vifm_vifmrc_for_vifmrc())
		return;
}

/* tries to use $MYVIFMRC as configuration file */
static int
try_myvifmrc_envvar_for_vifmrc(void)
{
	LOG_FUNC_ENTER;

	const char *myvifmrc = env_get(MYVIFMRC_EV);
	return myvifmrc != NULL && path_exists(myvifmrc);
}

/* tries to use vifmrc in directory of executable file as configuration file */
static int
try_exe_directory_for_vifmrc(void)
{
	LOG_FUNC_ENTER;

	char exe_dir[PATH_MAX];
	char vifmrc[PATH_MAX];

	if(get_exe_dir(exe_dir, sizeof(exe_dir)) != 0)
	{
		return 0;
	}

	snprintf(vifmrc, sizeof(vifmrc), "%s/" VIFMRC, exe_dir);
	if(!path_exists(vifmrc))
	{
		return 0;
	}

	env_set(MYVIFMRC_EV, vifmrc);
	return 1;
}

/* tries to use $VIFM/vifmrc as configuration file */
static int
try_vifm_vifmrc_for_vifmrc(void)
{
	LOG_FUNC_ENTER;

	char vifmrc[PATH_MAX];
	const char *vifm = env_get(VIFM_EV);
	if(vifm == NULL || !is_dir(vifm))
		return 0;
	snprintf(vifmrc, sizeof(vifmrc), "%s/" VIFMRC, vifm);
	if(!path_exists(vifmrc))
		return 0;
	env_set(MYVIFMRC_EV, vifmrc);
	return 1;
}

/* writes path configuration file and directories for further usage */
static void
store_config_paths(void)
{
	LOG_FUNC_ENTER;

	snprintf(cfg.home_dir, sizeof(cfg.home_dir), "%s/", env_get(HOME_EV));
	snprintf(cfg.config_dir, sizeof(cfg.config_dir), "%s", env_get(VIFM_EV));
	snprintf(cfg.trash_dir, sizeof(cfg.trash_dir), "%s/" TRASH, cfg.config_dir);
	snprintf(cfg.log_file, sizeof(cfg.log_file), "%s/" LOG, cfg.config_dir);
}

/* ensures existence of configuration directory */
static void
create_config_dir(void)
{
	LOG_FUNC_ENTER;

	/* ensure existence of configuration directory */
	if(!is_dir(cfg.config_dir))
	{
#ifndef _WIN32
		FILE *f;
		char help_file[PATH_MAX];
		char rc_file[PATH_MAX];

		if(make_dir(cfg.config_dir, 0777) != 0)
			return;

		snprintf(help_file, sizeof(help_file), "%s/" VIFM_HELP, cfg.config_dir);
		if((f = fopen(help_file, "r")) == NULL)
			create_help_file();
		else
			fclose(f);

		snprintf(rc_file, sizeof(rc_file), "%s/" VIFMRC, cfg.config_dir);
		if((f = fopen(rc_file, "r")) == NULL)
			create_rc_file();
		else
			fclose(f);

		/* This should be first start of Vifm, ensure that newly created sample
		 * vifmrc file is used right away. */
		env_set(MYVIFMRC_EV, rc_file);
#else
		if(make_dir(cfg.config_dir, 0777) != 0)
			return;
#endif

		add_default_bookmarks();
	}
}

#ifndef _WIN32
/* Copies help file from shared files to the ~/.vifm directory. */
static void
create_help_file(void)
{
	LOG_FUNC_ENTER;

	char command[] = CP_HELP;
	(void)vifm_system(command);
}

/* Copies example vifmrc file from shared files to the ~/.vifm directory. */
static void
create_rc_file(void)
{
	LOG_FUNC_ENTER;

	char command[] = CP_RC;
	(void)vifm_system(command);
}
#endif

/* Adds 'H' and 'z' default bookmarks. */
static void
add_default_bookmarks(void)
{
	LOG_FUNC_ENTER;

	set_user_bookmark('H', cfg.home_dir, NO_BOOKMARK_FILE);
	set_user_bookmark('z', cfg.config_dir, NO_BOOKMARK_FILE);
}

void
source_config(void)
{
	const char *const myvifmrc = env_get(MYVIFMRC_EV);
	if(myvifmrc != NULL)
	{
		(void)source_file(myvifmrc);
	}
}

int
source_file(const char filename[])
{
	FILE *fp;
	int result;
	SourcingState sourcing_state;

	if((fp = fopen(filename, "r")) == NULL)
		return 1;

	sourcing_state = curr_stats.sourcing_state;
	curr_stats.sourcing_state = SOURCING_PROCESSING;

	result = source_file_internal(fp, filename);

	curr_stats.sourcing_state = sourcing_state;

	fclose(fp);
	return result;
}

/* Returns non-zero on error. */
static int
source_file_internal(FILE *fp, const char filename[])
{
	char line[MAX_VIFMRC_LINE_LEN + 1];
	char *next_line = NULL;
	int line_num;
	int encoutered_errors = 0;

	if(fgets(line, sizeof(line), fp) == NULL)
	{
		/* File is empty. */
		return 0;
	}
	chomp(line);

	line_num = 1;
	for(;;)
	{
		char *p;
		int line_num_delta = 0;

		while((p = next_line = read_line(fp, next_line)) != NULL)
		{
			line_num_delta++;
			p = skip_whitespace(p);
			if(*p == '"')
				continue;
			else if(*p == '\\')
				strncat(line, p + 1, sizeof(line) - strlen(line) - 1);
			else
				break;
		}
		if(exec_commands(line, curr_view, GET_COMMAND) < 0)
		{
			show_sourcing_error(filename, line_num);
			encoutered_errors = 1;
		}
		if(curr_stats.sourcing_state == SOURCING_FINISHING)
			break;

		if(p == NULL)
		{
			/* Artificially increment line number to simulate as if all that happens
			 * after the loop relates to something past end of the file. */
			line_num++;
			break;
		}

		copy_str(line, sizeof(line), p);
		line_num += line_num_delta;
	}

	free(next_line);

	if(commands_block_finished() != 0)
	{
		show_sourcing_error(filename, line_num);
		encoutered_errors = 1;
	}

	return encoutered_errors;
}

/* Displays sourcing error message to a user. */
static void
show_sourcing_error(const char filename[], int line_num)
{
	/* User choice is saved by prompt_error_msgf internally. */
	(void)prompt_error_msgf("File Sourcing Error", "Error in %s at %d line",
			filename, line_num);
}

int
is_old_config(void)
{
	const char *const myvifmrc = env_get(MYVIFMRC_EV);
	return (myvifmrc != NULL) && is_conf_file(myvifmrc);
}

/* Checks whether file is configuration file (has at least one line which starts
 * with a hash symbol).  Returns non-zero if yes, otherwise zero is returned. */
static int
is_conf_file(const char file[])
{
	FILE *const fp = fopen(file, "r");
	char *line = NULL;

	if(fp != NULL)
	{
		while((line = read_line(fp, line)) != NULL)
		{
			if(skip_whitespace(line)[0] == '#')
			{
				break;
			}
		}
		fclose(fp);
		free(line);
	}
	return line != NULL;
}

int
are_old_color_schemes(void)
{
	char colors_dir[PATH_MAX];
	snprintf(colors_dir, sizeof(colors_dir), "%s/colors", cfg.config_dir);
	return !is_dir(colors_dir) && path_exists_at(cfg.config_dir, "colorschemes");
}

const char *
get_vicmd(int *bg)
{
	if(curr_stats.exec_env_type != EET_EMULATOR_WITH_X)
	{
		*bg = cfg.vi_cmd_bg;
		return cfg.vi_command;
	}
	else if(cfg.vi_x_command[0] != '\0')
	{
		*bg = cfg.vi_x_cmd_bg;
		return cfg.vi_x_command;
	}
	else
	{
		*bg = cfg.vi_cmd_bg;
		return cfg.vi_command;
	}
}

void
generate_tmp_file_name(const char prefix[], char buf[], size_t buf_len)
{
	snprintf(buf, buf_len, "%s/%s", get_tmpdir(), prefix);
#ifdef _WIN32
	to_forward_slash(buf);
#endif
	copy_str(buf, buf_len, make_name_unique(buf));
}

/* Returns path to tmp directory.  Uses environment variables to determine the
 * correct place. */
const char *
get_tmpdir(void)
{
	return env_get_one_of_def("/tmp/", "TMPDIR", "TEMP", "TEMPDIR", "TMP", NULL);
}

void
resize_history(size_t new_len)
{
	const int old_len = MAX(cfg.history_len, 0);
	const int delta = (int)new_len - old_len;

	if(new_len == 0)
	{
		disable_history();
		return;
	}

	if(delta < 0)
	{
		decrease_history(new_len, -delta);
	}

	reallocate_history(new_len);

	if(delta > 0)
	{
		zero_new_history_items(old_len, delta);
	}

	cfg.history_len = new_len;

	if(old_len == 0)
	{
		save_view_history(&lwin, NULL, NULL, -1);
		save_view_history(&rwin, NULL, NULL, -1);
	}
}

/* Completely disables all histories and clears all of them. */
static void
disable_history(void)
{
	free_view_history(&lwin);
	free_view_history(&rwin);

	(void)hist_reset(&cfg.search_hist, cfg.history_len);
	(void)hist_reset(&cfg.cmd_hist, cfg.history_len);
	(void)hist_reset(&cfg.prompt_hist, cfg.history_len);
	(void)hist_reset(&cfg.filter_hist, cfg.history_len);

	cfg.history_len = 0;
}

/* Clears and frees directory history of the view. */
static void
free_view_history(FileView *view)
{
	free_history_items(view->history, view->history_num);
	free(view->history);
	view->history = NULL;

	view->history_num = 0;
	view->history_pos = 0;
}

/* Reduces amount of memory taken by the history.  The new_len specifies new
 * size of the history, while removed_count parameter designates number of
 * removed elements. */
static void
decrease_history(size_t new_len, size_t removed_count)
{
	reduce_view_history(&lwin, new_len);
	reduce_view_history(&rwin, new_len);

	hist_trunc(&cfg.search_hist, new_len, removed_count);
	hist_trunc(&cfg.cmd_hist, new_len, removed_count);
	hist_trunc(&cfg.prompt_hist, new_len, removed_count);
	hist_trunc(&cfg.filter_hist, new_len, removed_count);
}

/* Moves items of directory history when size of history becomes smaller. */
static void
reduce_view_history(FileView *view, size_t size)
{
	const int delta = MIN(view->history_num - (int)size, view->history_pos);
	if(delta <= 0)
		return;

	free_history_items(view->history, MIN(size, delta));
	memmove(view->history, view->history + delta,
			sizeof(history_t)*(view->history_num - delta));

	if(view->history_num >= size)
		view->history_num = size - 1;
	view->history_pos -= delta;
}

/* Reallocates memory taken by history elements.  The new_len specifies new
 * size of the history. */
static void
reallocate_history(size_t new_len)
{
	const size_t hist_item_len = sizeof(history_t)*new_len;
	const size_t str_item_len = sizeof(char *)*new_len;

	lwin.history = realloc(lwin.history, hist_item_len);
	rwin.history = realloc(rwin.history, hist_item_len);

	cfg.cmd_hist.items = realloc(cfg.cmd_hist.items, str_item_len);
	cfg.search_hist.items = realloc(cfg.search_hist.items, str_item_len);
	cfg.prompt_hist.items = realloc(cfg.prompt_hist.items, str_item_len);
	cfg.filter_hist.items = realloc(cfg.filter_hist.items, str_item_len);
}

/* Zeroes new elements of the history.  The old_len specifies old history size,
 * while delta parameter designates number of new elements. */
static void
zero_new_history_items(size_t old_len, size_t delta)
{
	const size_t hist_item_len = sizeof(history_t)*delta;
	const size_t str_item_len = sizeof(char *)*delta;

	memset(lwin.history + old_len, 0, hist_item_len);
	memset(rwin.history + old_len, 0, hist_item_len);

	memset(cfg.cmd_hist.items + old_len, 0, str_item_len);
	memset(cfg.search_hist.items + old_len, 0, str_item_len);
	memset(cfg.prompt_hist.items + old_len, 0, str_item_len);
	memset(cfg.filter_hist.items + old_len, 0, str_item_len);
}

int
set_fuse_home(const char new_value[])
{
	char canonicalized[PATH_MAX];
#ifdef _WIN32
	char with_forward_slashes[strlen(new_value) + 1];
	strcpy(with_forward_slashes, new_value);
	to_forward_slash(with_forward_slashes);
	new_value = with_forward_slashes;
#endif
	canonicalize_path(new_value, canonicalized, sizeof(canonicalized));

	if(!is_path_absolute(new_value))
	{
		show_error_msgf("Error Setting FUSE Home Directory",
				"The path is not absolute: %s", canonicalized);
		return 1;
	}

	return replace_string(&cfg.fuse_home, canonicalized);
}

void
set_use_term_multiplexer(int use_term_multiplexer)
{
	cfg.use_term_multiplexer = use_term_multiplexer;
	set_using_term_multiplexer(use_term_multiplexer);
}

void
free_history_items(const history_t history[], size_t len)
{
	size_t i;
	for(i = 0; i < len; i++)
	{
		free(history[i].dir);
		free(history[i].file);
	}
}

void
save_command_history(const char command[])
{
	if(is_history_command(command))
	{
		update_last_cmdline_command(command);
		save_into_history(command, &cfg.cmd_hist, cfg.history_len);
	}
}

void
save_search_history(const char pattern[])
{
	save_into_history(pattern, &cfg.search_hist, cfg.history_len);
}

void
save_prompt_history(const char input[])
{
	save_into_history(input, &cfg.prompt_hist, cfg.history_len);
}

void
save_filter_history(const char input[])
{
	save_into_history(input, &cfg.filter_hist, cfg.history_len);
}

/* Adaptor for the hist_add() function, which handles signed history length. */
static void
save_into_history(const char item[], hist_t *hist, int len)
{
	if(len >= 0)
	{
		hist_add(hist, item, len);
	}
}

void
cfg_set_shell(const char shell[])
{
	if(replace_string(&cfg.shell, shell) == 0)
	{
		stats_update_shell_type(cfg.shell);
	}
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
