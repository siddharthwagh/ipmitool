/*
 * Copyright (c) 2003 Sun Microsystems, Inc.  All Rights Reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistribution of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * Redistribution in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * Neither the name of Sun Microsystems, Inc. or the names of
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * 
 * This software is provided "AS IS," without a warranty of any kind.
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND WARRANTIES,
 * INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE OR NON-INFRINGEMENT, ARE HEREBY EXCLUDED.
 * SUN MICROSYSTEMS, INC. ("SUN") AND ITS LICENSORS SHALL NOT BE LIABLE
 * FOR ANY DAMAGES SUFFERED BY LICENSEE AS A RESULT OF USING, MODIFYING
 * OR DISTRIBUTING THIS SOFTWARE OR ITS DERIVATIVES.  IN NO EVENT WILL
 * SUN OR ITS LICENSORS BE LIABLE FOR ANY LOST REVENUE, PROFIT OR DATA,
 * OR FOR DIRECT, INDIRECT, SPECIAL, CONSEQUENTIAL, INCIDENTAL OR
 * PUNITIVE DAMAGES, HOWEVER CAUSED AND REGARDLESS OF THE THEORY OF
 * LIABILITY, ARISING OUT OF THE USE OF OR INABILITY TO USE THIS SOFTWARE,
 * EVEN IF SUN HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 * 
 * You acknowledge that this software is not designed or intended for use
 * in the design, construction, operation or maintenance of any nuclear
 * facility.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include <ipmitool/ipmi.h>
#include <ipmitool/ipmi_intf.h>
#include <ipmitool/helper.h>
#include <ipmitool/log.h>
#include <ipmitool/ipmi_lanp.h>
#include <ipmitool/ipmi_channel.h>
#include <ipmitool/ipmi_strings.h>
#include <ipmitool/ipmi_constants.h>

void printf_channel_usage (void);

/**
 * ipmi_1_5_authtypes
 *
 * Create a string describing the supported authentication types as 
 * specificed by the parameter n
 */
static const char *
ipmi_1_5_authtypes(unsigned char n)
{
	unsigned int i;
	static char supportedTypes[128];

	bzero(supportedTypes, 128);

	for (i = 0; ipmi_authtype_vals[i].val != 0; i++) {
		if (n & ipmi_authtype_vals[i].val) {
			strcat(supportedTypes, ipmi_authtype_vals[i].str);
			strcat(supportedTypes, " ");
		}
	}

	return supportedTypes;
}



/**
 * ipmi_get_channel_auth_cap
 *
 * return 0 on success
 *        -1 on failure
 */
int
ipmi_get_channel_auth_cap(struct ipmi_intf * intf,
			  unsigned char channel,
			  unsigned char priv)
{
	struct ipmi_rs * rsp;
	struct ipmi_rq req;
	struct get_channel_auth_cap_rsp auth_cap;
	unsigned char msg_data[2];

	msg_data[0] = channel | 0x80; // Ask for IPMI v2 data as well
	msg_data[1] = priv;

	memset(&req, 0, sizeof(req));
	req.msg.netfn    = IPMI_NETFN_APP;            // 0x06
	req.msg.cmd      = IPMI_GET_CHANNEL_AUTH_CAP; // 0x38
	req.msg.data     = msg_data;
	req.msg.data_len = 2;

	rsp = intf->sendrecv(intf, &req);

	if ((rsp == NULL) || (rsp->ccode > 0))	{
		/*
		 * It's very possible that this failed because we asked for IPMI v2 data
		 * Ask again, without requesting IPMI v2 data
		 */
		msg_data[0] &= 0x7F;
		
		rsp = intf->sendrecv(intf, &req);
		if (rsp == NULL) {
			lprintf(LOG_ERR, "Unable to Get Channel Authentication Capabilities");
			return -1;
		}
		if (rsp->ccode > 0) {
			lprintf(LOG_ERR, "Get Channel Authentication Capabilities failed: %s",
				val2str(rsp->ccode, completion_code_vals));
			return -1;
		}
	}

	memcpy(&auth_cap, rsp->data, sizeof(struct get_channel_auth_cap_rsp));

	printf("Channel number             : %d\n",
		   auth_cap.channel_number);
	printf("IPMI v1.5  auth types      : %s\n",
		   ipmi_1_5_authtypes(auth_cap.enabled_auth_types));

	if (auth_cap.v20_data_available)
		printf("KG status                  : %s\n",
			   (auth_cap.kg_status) ? "non-zero" : "default (all zeroes)");

	printf("Per message authentication : %sabled\n",
		   (auth_cap.per_message_auth) ? "en" : "dis");
	printf("User level authentication  : %sabled\n",
		   (auth_cap.user_level_auth) ? "en" : "dis");

	printf("Non-null user names exist  : %s\n",
		   (auth_cap.non_null_usernames) ? "yes" : "no");
	printf("Null user names exist      : %s\n",
		   (auth_cap.null_usernames) ? "yes" : "no");
	printf("Anonymous login enabled    : %s\n",
		   (auth_cap.anon_login_enabled) ? "yes" : "no");

	if (auth_cap.v20_data_available) {
		printf("Channel supports IPMI v1.5 : %s\n",
			   (auth_cap.ipmiv15_support) ? "yes" : "no");
		printf("Channel supports IPMI v2.0 : %s\n",
			   (auth_cap.ipmiv20_support) ? "yes" : "no");
	}

	/*
	 * If there is support for an OEM authentication type, there is some
	 * information.
	 */
	if (auth_cap.enabled_auth_types & IPMI_1_5_AUTH_TYPE_BIT_OEM) {
		printf("IANA Number for OEM        : %d\n",
			   auth_cap.oem_id[0]      | 
			   auth_cap.oem_id[1] << 8 | 
			   auth_cap.oem_id[2] << 16);
		printf("OEM Auxiliary Data         : 0x%x\n",
			   auth_cap.oem_aux_data);
	}

    return 0;
}



/**
 * ipmi_get_channel_info
 *
 * returns 0 on success
 *         -1 on failure
 *
 */
int
ipmi_get_channel_info(struct ipmi_intf * intf, unsigned char channel)
{
	struct ipmi_rs * rsp;
	struct ipmi_rq req;
	unsigned char rqdata[2];
	struct get_channel_info_rsp   channel_info;
	struct get_channel_access_rsp channel_access;

	memset(&req, 0, sizeof(req));
	req.msg.netfn = IPMI_NETFN_APP;        // 0x06
	req.msg.cmd   = IPMI_GET_CHANNEL_INFO; // 0x42
	req.msg.data = &channel;
	req.msg.data_len = 1;

	rsp = intf->sendrecv(intf, &req);
	if (rsp == NULL) {
		lprintf(LOG_ERR, "Unable to Get Channel Info");
		return -1;
	}
	if (rsp->ccode > 0) {
		lprintf(LOG_ERR, "Get Channel Info failed: %s",
			val2str(rsp->ccode, completion_code_vals));
		return -1;
	}

	memcpy(&channel_info, rsp->data, sizeof(struct get_channel_info_rsp));

	printf("Channel 0x%x info:\n", channel_info.channel_number);

	printf("  Channel Medium Type   : %s\n",
		   val2str(channel_info.channel_medium, ipmi_channel_medium_vals));

	printf("  Channel Protocol Type : %s\n",
		   val2str(channel_info.channel_protocol, ipmi_channel_protocol_vals));

	printf("  Session Support       : ");
	switch (channel_info.session_support) {
		case 0x00:
			printf("session-less\n");
			break;
		case 0x40:
			printf("single-session\n");
			break;
		case 0x80:
			printf("multi-session\n");
			break;
		case 0xc0:
		default:
			printf("session-based\n");
			break;
	}

	printf("  Active Session Count  : %d\n",
		   channel_info.active_sessions);

	printf("  Protocol Vendor ID    : %d\n",
		   channel_info.vendor_id[0]      |
		   channel_info.vendor_id[1] << 8 |
		   channel_info.vendor_id[2] << 16);



	memset(&req, 0, sizeof(req));
	rqdata[0] = channel & 0xf;

	/* get volatile settings */

	rqdata[1] = 0x80; /* 0x80=active */
	req.msg.netfn = IPMI_NETFN_APP;          // 0x06
	req.msg.cmd   = IPMI_GET_CHANNEL_ACCESS; // 0x41
	req.msg.data = rqdata;
	req.msg.data_len = 2;

	rsp = intf->sendrecv(intf, &req);
	if (rsp == NULL) {
		lprintf(LOG_ERR, "Unable to Get Channel Access (volatile)");
		return -1;
	}
	if (rsp->ccode > 0) {
		lprintf(LOG_ERR, "Get Channel Access (volatile) failed: %s",
			val2str(rsp->ccode, completion_code_vals));
		return -1;
	}

	memcpy(&channel_access, rsp->data, sizeof(struct get_channel_access_rsp));


	printf("  Volatile(active) Settings\n");
	printf("    Alerting            : %sabled\n",
		   (channel_access.alerting) ? "dis" : "en");
	printf("    Per-message Auth    : %sabled\n",
		   (channel_access.per_message_auth) ? "dis" : "en");
	printf("    User Level Auth     : %sabled\n",
		   (channel_access.user_level_auth) ? "dis" : "en");

	printf("    Access Mode         : ");
	switch (channel_access.access_mode) {
		case 0:
			printf("disabled\n");
			break;
		case 1:
			printf("pre-boot only\n");
			break;
		case 2:
			printf("always available\n");
			break;
		case 3:
			printf("shared\n");
			break;
		default:
			printf("unknown\n");
			break;
	}

	/* get non-volatile settings */

	rqdata[1] = 0x40; /* 0x40=non-volatile */
	rsp = intf->sendrecv(intf, &req);
	if (rsp == NULL) {
		lprintf(LOG_ERR, "Unable to Get Channel Access (non-volatile)");
		return -1;
	}
	if (rsp->ccode > 0) {
		lprintf(LOG_ERR, "Get Channel Access (non-volatile) failed: %s",
			val2str(rsp->ccode, completion_code_vals));
		return -1;
	}

	memcpy(&channel_access, rsp->data, sizeof(struct get_channel_access_rsp));

	printf("  Non-Volatile Settings\n");
	printf("    Alerting            : %sabled\n",
		   (channel_access.alerting) ? "dis" : "en");
	printf("    Per-message Auth    : %sabled\n",
		   (channel_access.per_message_auth) ? "dis" : "en");
	printf("    User Level Auth     : %sabled\n",
		   (channel_access.user_level_auth) ? "dis" : "en");

	printf("    Access Mode         : ");
	switch (channel_access.access_mode) {
		case 0:
			printf("disabled\n");
			break;
		case 1:
			printf("pre-boot only\n");
			break;
		case 2:
			printf("always available\n");
			break;
		case 3:
			printf("shared\n");
			break;
		default:
			printf("unknown\n");
			break;
	}

	return 0;
}

static int
ipmi_get_user_access(struct ipmi_intf * intf, unsigned char channel, unsigned char userid)
{
	struct ipmi_rs * rsp;
	struct ipmi_rq req1, req2;
	unsigned char rqdata[2];
	struct get_user_access_rsp user_access;
	int curr_uid, max_uid = 0, init = 1;

	ipmi_intf_session_set_privlvl(intf, IPMI_SESSION_PRIV_ADMIN);

	curr_uid = userid ? : 1;

	memset(&req1, 0, sizeof(req1));
	req1.msg.netfn = IPMI_NETFN_APP;
	req1.msg.cmd = IPMI_GET_USER_ACCESS;
	req1.msg.data = rqdata;
	req1.msg.data_len = 2;

	memset(&req2, 0, sizeof(req2));
	req2.msg.netfn = IPMI_NETFN_APP;
	req2.msg.cmd = IPMI_GET_USER_NAME;
	req2.msg.data = rqdata;
	req2.msg.data_len = 1;

	do
	{
		rqdata[0] = channel & 0xf;
		rqdata[1] = curr_uid & 0x3f;

		rsp = intf->sendrecv(intf, &req1);
		if (rsp == NULL) {
			lprintf(LOG_ERR, "Unable to Get User Access (channel %d id %d)",
				rqdata[0], rqdata[1]);
			return -1;
		}
		if (rsp->ccode > 0) {
			lprintf(LOG_ERR, "Get User Access (channel %d id %d) failed: %s",
				rqdata[0], rqdata[1],
				val2str(rsp->ccode, completion_code_vals));
			return -1;
		}

		memcpy(&user_access, rsp->data, sizeof(struct get_user_access_rsp));

		rqdata[0] = curr_uid & 0x3f;

		rsp = intf->sendrecv(intf, &req2);
		if (rsp == NULL) {
			lprintf(LOG_ERR, "Unable to Get User Name (id %d)", rqdata[0]);
			return -1;
		}
		if (rsp->ccode > 0) {
			lprintf(LOG_ERR, "Get User Name (id %d) failed: %s",
				rqdata[0], val2str(rsp->ccode, completion_code_vals));
			return -1;
		}

		if (init) {
			printf("Maximum User IDs     : %d\n", user_access.max_user_ids);
			printf("Enabled User IDs     : %d\n", user_access.enabled_user_ids);
			max_uid = user_access.max_user_ids;
			init = 0;
		}

		printf("\n");
		printf("User ID              : %d\n", curr_uid);
		printf("User Name            : %s\n", rsp->data);
		printf("Fixed Name           : %s\n",
		       (curr_uid <= user_access.fixed_user_ids) ? "Yes" : "No");
		printf("Access Available     : %s\n",
		       (user_access.callin_callback) ? "callback" : "call-in / callback");
		printf("Link Authentication  : %sabled\n",
		       (user_access.link_auth) ? "en" : "dis");
		printf("IPMI Messaging       : %sabled\n",
		       (user_access.ipmi_messaging) ? "en" : "dis");
		printf("Privilege Level      : %s\n",
		       val2str(user_access.privilege_limit, ipmi_privlvl_vals));

		curr_uid ++;

	} while (!userid && curr_uid <= max_uid);

	return 0;
}

static int
ipmi_set_user_access(struct ipmi_intf * intf, int argc, char ** argv)
{
	unsigned char channel, userid;
	struct ipmi_rs * rsp;
	struct ipmi_rq req;
	unsigned char rqdata[2];
	struct get_user_access_rsp user_access;
	struct set_user_access_data set_access;
	int i;

	ipmi_intf_session_set_privlvl(intf, IPMI_SESSION_PRIV_ADMIN);

        if ((argc < 3) || (strncmp(argv[0], "help", 4) == 0)) {
		printf_channel_usage();
                return 0;
        }

	channel = (unsigned char)strtol(argv[0], NULL, 0);
	userid = (unsigned char)strtol(argv[1], NULL, 0);

	memset(&req, 0, sizeof(req));
	req.msg.netfn = IPMI_NETFN_APP;
	req.msg.cmd = IPMI_GET_USER_ACCESS;
	req.msg.data = rqdata;
	req.msg.data_len = 2;

	rqdata[0] = channel & 0xf;
	rqdata[1] = userid & 0x3f;

	rsp = intf->sendrecv(intf, &req);
	if (rsp == NULL) {
		lprintf(LOG_ERR, "Unable to Get User Access (channel %d id %d)",
			rqdata[0], rqdata[1]);
		return -1;
	}
	if (rsp->ccode > 0) {
		lprintf(LOG_ERR, "Get User Access (channel %d id %d) failed: %s",
			rqdata[0], rqdata[1],
			val2str(rsp->ccode, completion_code_vals));
		return -1;
	}

	memcpy(&user_access, rsp->data, sizeof(struct get_user_access_rsp));

	set_access.change_bits = 1;
	set_access.callin_callback = user_access.callin_callback;
	set_access.link_auth = user_access.link_auth;
	set_access.ipmi_messaging = user_access.ipmi_messaging;
	set_access.channel = channel;
	set_access.user_id = userid;
	set_access.privilege_limit = user_access.privilege_limit;
	set_access.session_limit = 0;

	for (i = 2; i < argc; i ++)
	{
		if (strncmp(argv[i], "callin=", 7) == 0) {
			set_access.callin_callback = !(strncmp (argv[i]+7, "off", 3));
		}
		else if (strncmp(argv[i], "link=", 5) == 0) {
			set_access.link_auth = strncmp (argv[i]+5, "off", 3);
		}
		else if (strncmp(argv[i], "ipmi=", 5) == 0) {
			set_access.ipmi_messaging = strncmp (argv[i]+5, "off", 3);
		}
		else if (strncmp(argv[i], "privilege=", 10) == 0) {
			set_access.privilege_limit = strtol (argv[i]+10, NULL, 0);
		}
		else {
			printf ("Invalid option: %s\n", argv [i]);
			return -1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.msg.netfn = IPMI_NETFN_APP;
	req.msg.cmd = IPMI_SET_USER_ACCESS;
	req.msg.data = (unsigned char *) &set_access;
	req.msg.data_len = 4;

	rsp = intf->sendrecv(intf, &req);
	if (rsp == NULL) {
		lprintf(LOG_ERR, "Unable to Set User Access (channel %d id %d)",
			set_access.channel, set_access.user_id);
		return -1;
	}
	if (rsp->ccode > 0) {
		lprintf(LOG_ERR, "Set User Access (channel %d id %d) failed: %s",
			set_access.channel, set_access.user_id,
			val2str(rsp->ccode, completion_code_vals));
		return -1;
	}

	return 0;
}

unsigned char
ipmi_get_channel_medium(struct ipmi_intf * intf, unsigned char channel)
{
	struct ipmi_rs * rsp;
	struct ipmi_rq req;
	struct get_channel_info_rsp info;

	memset(&req, 0, sizeof(req));
	req.msg.netfn = IPMI_NETFN_APP;
	req.msg.cmd = IPMI_GET_CHANNEL_INFO;
	req.msg.data = &channel;
	req.msg.data_len = 1;

	rsp = intf->sendrecv(intf, &req);
	if (rsp == NULL) {
		lprintf(LOG_ERR, "Get Channel Info command failed");
		return -1;
	}
	if (rsp->ccode > 0) {
		lprintf(LOG_ERR, "Get Channel Info command failed: %s",
		       val2str(rsp->ccode, completion_code_vals));
		return -1;
	}

	memcpy(&info, rsp->data, sizeof(struct get_channel_info_rsp));

	lprintf(LOG_DEBUG, "Channel type: %s",
		val2str(info.channel_medium, ipmi_channel_medium_vals));

	return info.channel_medium;
}

unsigned char
ipmi_current_channel_medium(struct ipmi_intf * intf)
{
	return ipmi_get_channel_medium(intf, 0xE);
}

void
printf_channel_usage()
{
	lprintf(LOG_NOTICE, "Channel Commands: authcap   <channel number> <max privilege>");
	lprintf(LOG_NOTICE, "                  getaccess <channel number> [user id]");
	lprintf(LOG_NOTICE, "                  setaccess <channel number> "
		"<user id> [callin=on|off] [ipmi=on|off] [link=on|off] [privilege=level]");
	lprintf(LOG_NOTICE, "                  info      [channel number]\n");
	lprintf(LOG_NOTICE, "Possible privilege levels are:");
	lprintf(LOG_NOTICE, "   1   Callback level");
	lprintf(LOG_NOTICE, "   2   User level");
	lprintf(LOG_NOTICE, "   3   Operator level");
	lprintf(LOG_NOTICE, "   4   Administrator level");
	lprintf(LOG_NOTICE, "   5   OEM Proprietary level");
	lprintf(LOG_NOTICE, "  15   No access");
}


int
ipmi_channel_main(struct ipmi_intf * intf, int argc, char ** argv)
{
	int retval = 0;

	if ((argc == 0) || (strncmp(argv[0], "help", 4) == 0))
	{
		printf_channel_usage();
	}
	else if (strncmp(argv[0], "authcap", 7) == 0)
	{
		if (argc != 3)
			printf_channel_usage();
		else
			retval = ipmi_get_channel_auth_cap(intf,
                                               (unsigned char)strtol(argv[1], NULL, 0),
                                               (unsigned char)strtol(argv[2], NULL, 0));
	}
	else if (strncmp(argv[0], "getaccess", 10) == 0)
	{
		if ((argc < 2) || (argc > 3))
			printf_channel_usage();
		else {
			unsigned char ch = (unsigned char)strtol(argv[1], NULL, 0);
			unsigned char id = 0;
			if (argc == 3)
				id = (unsigned char)strtol(argv[2], NULL, 0);
			retval = ipmi_get_user_access(intf, ch, id);
		}
	}
	else if (strncmp(argv[0], "setaccess", 9) == 0)
	{
		retval = ipmi_set_user_access(intf, argc-1, &(argv[1]));
	}
	else if (strncmp(argv[0], "info", 4) == 0)
	{
		if (argc > 2)
			printf_channel_usage();
		else {
			unsigned char ch = 0xe;
			if (argc == 2)
				ch = (unsigned char)strtol(argv[1], NULL, 0);
			retval = ipmi_get_channel_info(intf, ch);
		}
	}
	else
	{
		printf("Invalid CHANNEL command: %s\n", argv[0]);
		printf_channel_usage();
		retval = -1;
	}

	return retval;
}

