/*
 * libwebsockets web server application
 *
 * Copyright (C) 2010-2016 Andy Green <andy@warmcat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation:
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301  USA
 */

#include "lwsws.h"

static struct lws_context *context;

#define LWSWS_CONFIG_STRING_SIZE (32 * 1024)

static const struct lws_extension exts[] = {
	{
		"permessage-deflate",
		lws_extension_callback_pm_deflate,
		"permessage-deflate"
	},
	{ NULL, NULL, NULL /* terminator */ }
};

static const char * const plugin_dirs[] = {
	INSTALL_DATADIR"/libwebsockets-test-server/plugins/",
	NULL
};

static struct option options[] = {
	{ "help",	no_argument,		NULL, 'h' },
	{ "debug",	required_argument,	NULL, 'd' },
	{ "configdir",  required_argument,	NULL, 'c' },
#ifndef LWS_NO_DAEMONIZE
	{ "daemonize", 	no_argument,		NULL, 'D' },
#endif
	{ NULL, 0, 0, 0 }
};

void signal_cb(uv_signal_t *watcher, int signum)
{
	lwsl_err("Signal %d caught, exiting...\n", watcher->signum);
	switch (watcher->signum) {
	case SIGTERM:
	case SIGINT:
		break;
	default:
		signal(SIGABRT, SIG_DFL);
		abort();
		break;
	}
	lws_libuv_stop(context);
}

int main(int argc, char **argv)
{
	struct lws_context_creation_info info;
	char *cs;
	int opts = 0, cs_len = LWSWS_CONFIG_STRING_SIZE - 1;
	int n = 0;
#ifndef _WIN32
	int syslog_options = LOG_PID | LOG_PERROR;
#endif
#ifndef LWS_NO_DAEMONIZE
 	int daemonize = 0;
#endif
	int debug_level = 7;
	char *config_dir = "/etc/lwsws/conf.d";
	char *config_strings;

	memset(&info, 0, sizeof info);

	while (n >= 0) {
		n = getopt_long(argc, argv, "hd:c:D", options, NULL);
		if (n < 0)
			continue;
		switch (n) {
#ifndef LWS_NO_DAEMONIZE
		case 'D':
			daemonize = 1;
			#ifndef _WIN32
			syslog_options &= ~LOG_PERROR;
			#endif
			printf("Daemonizing...\n");
			break;
#endif
		case 'd':
			debug_level = atoi(optarg);
			break;
		case 'c':
			strncpy(config_dir, optarg, sizeof(config_dir) - 1);
			config_dir[sizeof(config_dir) - 1] = '\0';
			break;
		case 'h':
			fprintf(stderr, "Usage: lwsws [-c <config dir>] "
					"[-d <log bitfield>] [-D] [--help]\n");
			exit(1);
		}
	}

#if !defined(LWS_NO_DAEMONIZE) && !defined(WIN32)
	/*
	 * normally lock path would be /var/lock/lwsts or similar, to
	 * simplify getting started without having to take care about
	 * permissions or running as root, set to /tmp/.lwsts-lock
	 */
	if (daemonize && lws_daemonize("/tmp/.lwsts-lock")) {
		fprintf(stderr, "Failed to daemonize\n");
		return 10;
	}
	if (daemonize)
		lwsl_notice("Daemonized\n");
#endif

#ifndef _WIN32
	/* we will only try to log things according to our debug_level */
	setlogmask(LOG_UPTO (LOG_DEBUG));
	openlog("lwsws", syslog_options, LOG_DAEMON);
#endif

	lws_set_log_level(debug_level, lwsl_emit_syslog);

	lwsl_notice("lwsws libwebsockets web server - license GPL2.1\n");
	lwsl_notice("(C) Copyright 2010-2016 Andy Green <andy@warmcat.com>\n");

	cs = config_strings = malloc(LWSWS_CONFIG_STRING_SIZE);
	if (!config_strings) {
		lwsl_err("Unable to allocate config strings heap\n");
		return -1;
	}

	memset(&info, 0, sizeof(info));

	info.max_http_header_pool = 16;
	info.options = opts | LWS_SERVER_OPTION_VALIDATE_UTF8 |
			      LWS_SERVER_OPTION_EXPLICIT_VHOSTS |
			      LWS_SERVER_OPTION_LIBUV;

	info.plugin_dirs = plugin_dirs;
	lwsl_notice("Using config dir: \"%s\"\n", config_dir);

	/*
	 *  first go through the config for creating the outer context
	 */
	if (lwsws_get_config_globals(&info, config_dir, &cs, &cs_len))
		goto init_failed;

	context = lws_create_context(&info);
	if (context == NULL) {
		lwsl_err("libwebsocket init failed\n");
		goto init_failed;
	}

	/*
	 * then create the vhosts... protocols are entirely coming from
	 * plugins, so we leave it NULL
	 */

	info.extensions = exts;

	if (!lwsws_get_config_vhosts(context, &info, config_dir,
				     &cs, &cs_len)) {

		/* run the server */

		lws_uv_sigint_cfg(context, 1, signal_cb);
		lws_uv_initloop(context, NULL, 0);

		lws_libuv_run(context, 0);
	}

	lws_context_destroy(context);
	free(config_strings);

	fprintf(stderr, "lwsws exited cleanly\n");

#ifndef _WIN32
	closelog();
#endif

	return 0;

init_failed:
	free(config_strings);

	return 1;
}
