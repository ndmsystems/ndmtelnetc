#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <inttypes.h>
#include <ndmtelnet/xml.h>
#include <ndmtelnet/code.h>
#include <ndmtelnet/telnet.h>

#if defined(_WIN32) || defined(_WIN64)

#include <WinSock2.h>
#include <Ws2tcpip.h>

#else /* _WIN32 || _WIN64 */

#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static inline int fopen_s(FILE **fp,
						  const char *const file_name,
						  const char *const mode)
{
	*fp = fopen(file_name, mode);

	if (*fp == NULL) {
		return errno;
	}

	return 0;
}

#endif /* _WIN32 || _WIN64 */

#define NDM_TELNET_COMMAND_MAX					4096

static inline char __ndm_telnet_result_type(const ndm_code_t code)
{
	if (NDM_SUCCEEDED(code)) {
		return NDM_WARNING(code) ? 'W' : 'I';
	}

	return NDM_CRITICAL(code) ? 'C' : 'E';
}

static inline void __ndm_telnet_dump_ident(const size_t ident)
{
	size_t i = 0;

	for (; i< ident * 4; i++) {
		putchar(' ');
	}
}

static inline void
__ndm_telnet_dump_escaped(const char *const value)
{
	const char *p = value;

	for (; *p != '\0'; p++) {
		const char ch = *p;

		if (ch == '<') {
			printf("&lt;");
		} else if (ch == '>') {
			printf("&gt;");
		} else if (ch == '\'') {
			printf("&apos;");
		} else if (ch == '"') {
			printf("&quot;");
		} else if (ch == '&') {
			printf("&amp;");
		} else {
			putchar(ch);
		}
	}
}

static inline void
__ndm_telnet_dump_result(const struct ndm_xml_elem_t *response,
						 const size_t ident)
{
	const struct ndm_xml_elem_t *e = response;

	while (e != NULL) {
		const struct ndm_xml_attr_t *a = e->attributes.head;

		__ndm_telnet_dump_ident(ident);

		printf("<%s", e->name);

		while (a != NULL) {
			printf(" %s=\"", a->name);
			__ndm_telnet_dump_escaped(a->value);
			putchar('\"');
			a = a->next;
		}

		putchar('>');

		if (e->children.head == NULL) {
			__ndm_telnet_dump_escaped(e->value);
		} else {
			putchar('\n');
			__ndm_telnet_dump_result(e->children.head, ident + 1);
			__ndm_telnet_dump_ident(ident);
		}

		printf("</%s>\n", e->name);

		e = e->next;
	}
}

static inline unsigned int __ndm_telnet_ms_to(const int64_t deadline)
{
	const int64_t now = ndm_telnet_now();

	if (deadline > now) {
		return (unsigned int) (deadline - now);
	}

	return 0;
}

static inline enum ndm_telnet_err_t
__ndm_telnet_execute(struct ndm_telnet_t *telnet,
					 const char *const command,
					 const unsigned int timeout,
					 const bool show_responses)
{
	bool event = false;
	bool continued = false;
	const int64_t beg = ndm_telnet_now();
	const int64_t deadline = beg + timeout;
	enum ndm_telnet_err_t err;

	err = ndm_telnet_send(telnet, command, __ndm_telnet_ms_to(deadline));

	if (err != NDM_TELNET_ERR_OK) {
		fprintf(stderr, "Failed to send a command: %s (%i).\n",
				ndm_telnet_strerror(err), err);

		return err;
	}

	do {
		int64_t duration;
		ndm_code_t response_code = 0;
		const char *response_text = "";
		struct ndm_xml_elem_t *response = NULL;

		/**
		 * Continued commands like "show log" or "tools ping ..."
		 * may send multiple responses.
		 **/
		err = ndm_telnet_recv(telnet, &continued, &response_code,
							  &response_text, &response,
							  __ndm_telnet_ms_to(deadline));

		if (err != NDM_TELNET_ERR_OK) {
			fprintf(stderr, "Failed to receive a response: %s (%i).\n",
					ndm_telnet_strerror(err), err);

			return err;
		}

		if (NDM_FAILED(response_code)) {
			fprintf(stderr, "Failed to execute: 0x%08" PRIx32 ", %s\n",
					response_code, response_text);
			ndm_xml_doc_free(&response);

			break;
		}

		if (show_responses) {
			__ndm_telnet_dump_result(response, 0);
		}

		/**
		* Skip an asynchonous event (there is only the one event now:
		* a notification about user credentials changes raised by
		* "user ..." commands).
		**/
		event = (strcmp(response->name, "event") == 0);

		duration = ndm_telnet_now() - beg;
		printf("%c (%08" NDM_CODE_PRIX ") "
			   "[%03" PRIi64 ".%03" PRIi64 "] %s%s%s\n",
			   __ndm_telnet_result_type(response_code), response_code,
			   duration / 1000, duration % 1000, command,
			   continued ? " (continued)" : (event ? " (event)" : ""),
			   show_responses ? "\n" : "");

		ndm_xml_doc_free(&response);
	} while (continued || event);

	return NDM_TELNET_ERR_OK;
}

static inline bool __ndm_telnet_no_arg(const char *const arg_name,
									   const char opt)
{
	fprintf(stderr, "%s value required for \"-%c\" option.\n",
			arg_name, opt);

	return false;
}

static inline bool __ndm_telnet_get_ulong(const char *const arg,
										  unsigned long *l)
{
	char *end = NULL;

	if (strlen(arg) == 0 || !isdigit(arg[0])) {
		return false;
	}

	errno = 0;
	*l = strtoul(arg, &end, 10);

	if (errno != 0 || end == NULL || *end != '\0') {
		return false;
	}

	return true;
}

static inline const char *ip_address(const struct sockaddr_in *sin)
{
	static char ip[INET6_ADDRSTRLEN];
	struct in_addr addr = sin->sin_addr;

	return inet_ntop(sin->sin_family, &addr, ip, sizeof(ip));
}

static inline bool __ndm_telnet_parse_arguments(
		const int argc,
		char *const argv[],
		struct sockaddr_in *sin,
		const char **user,
		const char **password,
		unsigned int *timeout,
		const char **command,
		const char **file_name,
		bool *show_responses)
{
	int opt_idx = 1;

	if (argc <= 1) {
		printf("NDM telnet client options:\n" \
			   "    -A {address}   device address (%s)\n" \
			   "    -P {port}      telnet port (%" PRIu16 ")\n" \
			   "    -u {user}      user name (\"%s\")\n" \
			   "    -p {password}  user password (\"%s\")\n" \
			   "    -t {timeout}   I/O timeout in milliseconds (%u)\n" \
			   "    -c {command}   command to execute (\"%s\")\n" \
			   "    -f {file name} file name with a command set (\"%s\")\n" \
			   "    -s             show XML responses (%s)\n",
			   ip_address(sin), ntohs(sin->sin_port),
			   *user, *password, *timeout, *command,
			   *file_name, *show_responses ? "yes" : "no");

		return false;
	}

	while (opt_idx < argc) {
		const char *const opt_str = argv[opt_idx++];
		const char *arg;
		char opt;

		if (*opt_str != '-') {
			fprintf(stderr, "Invalid argument: \"%s\".\n", opt_str);
			return false;
		}

		if (strlen(opt_str + 1) > 1) {
			fprintf(stderr, "Invalid option: \"%s\".\n", opt_str);
			return false;
		}

		opt = *(opt_str + 1);

		if (opt == 's') {
			*show_responses = true;
			continue;
		}

		if (opt_idx == argc) {
			switch (opt) {
				case 'A': {
					return __ndm_telnet_no_arg("An address", opt);
				}

				case 'P': {
					return __ndm_telnet_no_arg("A port", opt);
				}

				case 'u': {
					return __ndm_telnet_no_arg("A user name", opt);
				}

				case 'p': {
					return __ndm_telnet_no_arg("A password", opt);
				}

				case 't': {
					return __ndm_telnet_no_arg("A delay", opt);
				}

				case 'c': {
					return __ndm_telnet_no_arg("A command", opt);
				}

				case 'f': {
					return __ndm_telnet_no_arg("A command", opt);
				}
			}

			fprintf(stderr, "Unknown option: \"%s\".\n", opt_str);
			return false;
		}

		arg = argv[opt_idx++];

		if (arg[0] == '-') {
			fprintf(stderr, "\"%s\" option has no argument.\n", opt_str);
			return false;
		}

		switch (opt) {
			case 'A': {
				struct in_addr saddr = { 0 };

				if (inet_pton(AF_INET, arg, &saddr) != 1) {
					fprintf(stderr, "Invalid IP address: \"%s\".\n", arg);
					return false;
				}

				sin->sin_addr = saddr;
				break;
			}

			case 'P': {
				unsigned long l = 0;

				if (!__ndm_telnet_get_ulong(arg, &l) || l > UINT16_MAX) {
					fprintf(stderr, "Invalid port: \"%s\".\n", arg);
					return false;
				}

				sin->sin_port = ntohs((uint16_t) l);
				break;
			}

			case 'u': {
				*user = arg;
				break;
			}

			case 'p': {
				*password = arg;
				break;
			}

			case 't': {
				unsigned long l = 0;

				if (!__ndm_telnet_get_ulong(arg, &l) || l > UINT_MAX) {
					fprintf(stderr, "Invalid timeout value: \"%s\".\n", arg);
					return false;
				}

				*timeout = (unsigned int) l;
				break;
			}

			case 'c': {
				*command = arg;
				break;
			}

			case 'f': {
				*file_name = arg;
				break;
			}

			default: {
				fprintf(stderr,
						"Unknown option \"%s\" with argument \"%s\".\n",
						opt_str, arg);
				return false;
			}
		}
	}

	return true;
}

static inline int __ndm_telnet_run(const struct sockaddr_in *sin,
								   const char *const user,
								   const char *const password,
								   const unsigned timeout,
								   const char *const command,
								   const char *const file_name,
								   const bool show_responses)
{
	struct ndm_telnet_t *telnet = NULL;
	enum ndm_telnet_err_t err = NDM_TELNET_ERR_OK;
	bool interactive = false;
	int64_t duration = 0;
	FILE *fp = NULL;
	int exit_code = EXIT_FAILURE;
	const int64_t beg = ndm_telnet_now();
#if defined(_WIN32) || defined(_WIN64)
	WSADATA wsa_data;
	const WORD version = MAKEWORD(2, 2);

	if (WSAStartup(version, &wsa_data) != 0) {
		fprintf(stderr, "Unable to initialize a socket library.\n");
		goto exit;
	}

	if (wsa_data.wVersion != version) {
		fprintf(stderr, "Unable to find a socket library v%u.%u.\n",
			    (unsigned int) LOBYTE(version),
				(unsigned int) HIBYTE(version));
		goto cleanup;
	}
#endif /* _WIN32 || _WIN64 */

	printf("Connecting to %s@%s:%" PRId16 "...\n\n",
		   user, ip_address(sin), ntohs(sin->sin_port));

	err = ndm_telnet_open(&telnet, sin, user, password, timeout);

	if (err != NDM_TELNET_ERR_OK) {
		fprintf(stderr, "Unable to open a telnet session: %s (%i).\n",
				ndm_telnet_strerror(err), err);
		goto error;
	}

	if (strlen(command) > 0) {
		err = __ndm_telnet_execute(telnet, command, timeout, show_responses);

		if (err != NDM_TELNET_ERR_OK) {
			goto error;
		}
	} else {
		char cmd[NDM_TELNET_COMMAND_MAX];

		if (strlen(file_name) == 0) {
			fp = stdin;
			interactive = true;
			printf("Connected in an interactive mode, type a command.\n\n");
		} else if (fopen_s(&fp, file_name, "r") != 0) {
			fprintf(stderr, "Unable to open \"%s\".\n", file_name);
			goto error;
		}

		while (fgets(cmd, sizeof(cmd), fp) != NULL) {
			size_t len;
			const char *p = cmd;

			while (isspace(*p)) {
				p++;
			}

			if (*p == '\0') {
				continue;
			}

			len = strlen(cmd);

			if (cmd[len - 1] != '\n' && !feof(fp)) {
				fprintf(stderr,
						"Error reading a file: \"%s\" command truncated.\n",
						cmd);
				goto error;
			}

			cmd[len - 1] = '\0';
			err = __ndm_telnet_execute(telnet, cmd, timeout, show_responses);

			if (err != NDM_TELNET_ERR_OK) {
				goto error;
			}

			if (interactive) {
				printf("\n");
			}
		}

		if (ferror(fp)) {
			fprintf(stderr, "Unable to read \"%s\".\n", file_name);
			goto error;
		}
	}

	exit_code = EXIT_SUCCESS;

error:
	if (fp != NULL && fp != stdin) {
		fclose(fp);
	}

	ndm_telnet_close(&telnet);

#if defined(_WIN32) || defined(_WIN64)
cleanup:
	WSACleanup();

exit:
#endif /* _WIN32 || _WIN64 */

	duration = ndm_telnet_now() - beg;

	printf("%sDone in %03" PRIi64 ".%03" PRIi64 "s.\n",
		   show_responses ? "" : "\n",
		   duration / 1000, duration % 1000);

	return exit_code;
}

static inline bool
__ndm_telnet_is_unicast(const struct in_addr *const addr)
{
	const unsigned long a = ntohl(addr->s_addr);

	if (a == 0x00000000 || a == 0xffffffff) {
		return false;
	}

	return (a & 0xf0000000) != 0xe0000000;
}

int main(int argc, char *argv[])
{
	struct sockaddr_in sin = { 0 };
	const char *user = NDM_TELNET_DEF_USER;
	const char *password = NDM_TELNET_DEF_PASSWORD;
	unsigned int timeout = NDM_TELNET_DEF_TIMEOUT;
	const char *command = "";
	const char *file_name = "";
	bool show_responses = false;

	printf("Simple NDM telnet client.\n");

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(NDM_TELNET_DEF_ADDRESS);
	sin.sin_port = htons(NDM_TELNET_DEF_PORT);

	if (!__ndm_telnet_parse_arguments(argc, argv, &sin, &user, &password,
									  &timeout, &command, &file_name,
									  &show_responses)) {
		return EXIT_FAILURE;
	}

	/* check arguments */

	if (timeout < NDM_TELNET_MIN_TIMEOUT ||
		timeout > NDM_TELNET_MAX_TIMEOUT) {
		fprintf(stderr,
				"A timeout value should be between [%i, %i] milliseconds.\n",
				NDM_TELNET_MIN_TIMEOUT, NDM_TELNET_MAX_TIMEOUT);
		return EXIT_FAILURE;
	}

	if (!__ndm_telnet_is_unicast(&sin.sin_addr)) {
		fprintf(stderr, "%s IP address is not unicast.\n", ip_address(&sin));
		return EXIT_FAILURE;
	}

	if (strlen(command) > 0 && strlen(file_name) > 0) {
		fprintf(stderr, "Both a command and a file name specified.\n");
		return EXIT_FAILURE;
	}

	return __ndm_telnet_run(&sin, user, password, timeout,
							command, file_name, show_responses);
}
