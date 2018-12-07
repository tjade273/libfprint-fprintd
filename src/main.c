/*
 * fprint D-Bus daemon
 * Copyright (C) 2008 Daniel Drake <dsd@gentoo.org>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <poll.h>
#include <stdlib.h>

#include <dbus/dbus-glib-bindings.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <libfprint/fprint.h>
#include <glib-object.h>
#include <gmodule.h>

#include "fprintd.h"
#include "loop.h"
#include "storage.h"
#include "file_storage.h"

extern DBusGConnection *fprintd_dbus_conn;
static gboolean no_timeout = FALSE;
static gboolean g_fatal_warnings = FALSE;

static void
set_storage_file (void)
{
	store.init = &file_storage_init;
	store.deinit = &file_storage_deinit;
	store.print_data_save = &file_storage_print_data_save;
	store.print_data_load = &file_storage_print_data_load;
	store.print_data_delete = &file_storage_print_data_delete;
	store.discover_prints = &file_storage_discover_prints;
}

static gboolean
load_storage_module (const char *module_name)
{
	GModule *module;
	char *filename;

	filename = g_module_build_path (PLUGINDIR, module_name);
	module = g_module_open (filename, 0);
	g_free (filename);
	if (module == NULL)
		return FALSE;

	if (!g_module_symbol (module, "init", (gpointer *) &store.init) ||
	    !g_module_symbol (module, "deinit", (gpointer *) &store.deinit) ||
	    !g_module_symbol (module, "print_data_save", (gpointer *) &store.print_data_save) ||
	    !g_module_symbol (module, "print_data_load", (gpointer *) &store.print_data_load) ||
	    !g_module_symbol (module, "print_data_delete", (gpointer *) &store.print_data_delete) ||
	    !g_module_symbol (module, "discover_prints", (gpointer *) &store.discover_prints)) {
	    	g_module_close (module);
	    	return FALSE;
	}

	g_module_make_resident (module);

	return TRUE;
}

static gboolean
load_conf (void)
{
	GKeyFile *file;
	char *filename;
	char *module_name;
	GError *error = NULL;
	gboolean ret;

	filename = g_build_filename (SYSCONFDIR, "fprintd.conf", NULL);
	file = g_key_file_new ();
	if (!g_key_file_load_from_file (file, filename, G_KEY_FILE_NONE, &error)) {
		g_print ("Could not open fprintd.conf: %s\n", error->message);
		goto bail;
	}

	g_free (filename);
	filename = NULL;

	module_name = g_key_file_get_string (file, "storage", "type", &error);
	if (module_name == NULL)
		goto bail;

	g_key_file_free (file);

	if (g_str_equal (module_name, "file")) {
		g_free (module_name);
		set_storage_file ();
		return TRUE;
	}

	ret = load_storage_module (module_name);
	g_free (module_name);

	return ret;

bail:
	g_key_file_free (file);
	g_free (filename);
	g_error_free (error);

	return FALSE;
}

static const GOptionEntry entries[] = {
	{"g-fatal-warnings", 0, 0, G_OPTION_ARG_NONE, &g_fatal_warnings, "Make all warnings fatal", NULL},
	{"no-timeout", 't', 0, G_OPTION_ARG_NONE, &no_timeout, "Do not exit after unused for a while", NULL},
	{ NULL }
};

int main(int argc, char **argv)
{
	GOptionContext *context;
	GMainLoop *loop;
	GError *error = NULL;
	FprintManager *manager;
	DBusGProxy *driver_proxy;
	guint32 request_name_ret;
	int r = 0;

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	context = g_option_context_new ("Fingerprint handler daemon");
	g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);

#if !GLIB_CHECK_VERSION (2, 36, 0)
	g_type_init();
#endif

	if (g_option_context_parse (context, &argc, &argv, &error) == FALSE) {
		g_print ("couldn't parse command-line options: %s\n", error->message);
		g_error_free (error);
		return 1;
	}

	if (g_fatal_warnings) {
		GLogLevelFlags fatal_mask;

		fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
		fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
		g_log_set_always_fatal (fatal_mask);
	}

	/* Obtain a connection to the session bus */
	fprintd_dbus_conn = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
	if (fprintd_dbus_conn == NULL) {
		g_warning("Failed to open connection to bus: %s", error->message);
		return 1;
	}

	driver_proxy = dbus_g_proxy_new_for_name(fprintd_dbus_conn,
		DBUS_SERVICE_DBUS, DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS);


	if (!org_freedesktop_DBus_request_name(driver_proxy, FPRINT_SERVICE_NAME,
			0, &request_name_ret, &error)) {
		g_warning("Failed to get name: %s", error->message);
		return 1;
	}

	if (request_name_ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		g_warning ("Got result code %u from requesting name", request_name_ret);
		return 1;
	}

	/* Load the configuration file,
	 * and the default storage plugin */
	if (!load_conf())
		set_storage_file ();
	store.init ();

	r = fp_init();
	if (r < 0) {
		g_warning("fprint init failed with error %d\n", r);
		return r;
	}

	loop = g_main_loop_new(NULL, FALSE);

	r = setup_pollfds();
	if (r < 0) {
		g_print("pollfd setup failed\n");
		goto err;
	}

	g_debug("Launching FprintObject");

	/* create the one instance of the Manager object to be shared between
	 * all fprintd users */
	manager = fprint_manager_new(no_timeout);

	g_debug("D-Bus service launched with name: %s", FPRINT_SERVICE_NAME);

	g_debug("entering main loop");
	g_main_loop_run(loop);
	g_debug("main loop completed");

	g_object_unref (manager);

err:
	fp_exit();
	return 0;
}

