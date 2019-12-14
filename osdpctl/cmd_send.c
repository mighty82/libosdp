/*
 * Copyright (c) 2019 Siddharth Chandrasekaran
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include "common.h"

int msgq_send_command(struct config_s *c, struct osdpctl_cmd *cmd)
{
	key_t key;

	key = ftok(c->config_file, 23);
	c->cs_send_msgid = msgget(key, 0666 | IPC_CREAT);
	if (c->cs_send_msgid < 0) {
		printf("Error: failed to create send msgq\n");
		exit(-1);
	}

	msgq_cmd.mtype = 1;
	memcpy(msgq_cmd.mtext, cmd, sizeof(struct osdpctl_cmd));

	return msgsnd(c->cs_send_msgid, &msgq_cmd, 1024, 0);
}

int handle_cmd_led(int argc, char *argv[], struct osdp_cmd_led *c)
{
	int color, blink = 0, count, state;

	if (argc != 3) {
		printf("Usage: led <color> <blink|static> <count|state>\n");
		return -2;
	}

	if (strcmp("red", argv[0]) == 0)
		color = OSDP_LED_COLOR_RED;
	else if (strcmp("green", argv[0]) == 0)
		color = OSDP_LED_COLOR_GREEN;
	else if (strcmp("amber", argv[0]) == 0)
		color = OSDP_LED_COLOR_AMBER;
	else if (strcmp("blue", argv[0]) == 0)
		color = OSDP_LED_COLOR_BLUE;
	else if (strcmp("none", argv[0]) == 0)
		color = OSDP_LED_COLOR_NONE;
	else
		return -1;

	if (strcmp("blink", argv[1]) == 0) {
		blink = 1;
		if (safe_atoi(argv[2], &count))
			return -1;
	} else if (strcmp("static", argv[1]) == 0) {
		if (safe_atoi(argv[2], &state))
			return -1;
	} else {
		return -1;
	}

	if (blink && count) {
		// Non infinite sequence.
		c->temporary.control_code = 1;
		c->temporary.on_count = 5;
		c->temporary.off_count = 5;
		c->temporary.on_color = color;
		c->temporary.off_color = OSDP_LED_COLOR_NONE;
		c->temporary.timer = (uint16_t)(10 * count);
	} else {
		// infinite sequence.
		c->permanent.control_code = 1;
		if (blink) {
			c->permanent.on_count = 5;
			c->permanent.off_count = 5;
			c->permanent.on_color = color;
			c->permanent.off_color = OSDP_LED_COLOR_NONE;
		} else {
			c->permanent.off_count = 0;
			c->permanent.off_color = OSDP_LED_COLOR_NONE;
			if (state == 1) {
				c->permanent.on_count = 5;
				c->permanent.on_color = color;
			} else {
				c->permanent.on_count = 0;
				c->permanent.on_color = OSDP_LED_COLOR_NONE;
			}
		}
	}

	return 0;
}

int handle_cmd_buzzer(int argc, char *argv[], struct osdp_cmd_buzzer *c)
{
	int blink = 0, count, state = 0;

	if (argc != 2) {
		printf("Usage: buzzer <blink|static> <count|state>\n");
		return -2;
	}

	if (strcmp("blink", argv[0]) == 0) {
		blink = 1;
		if (safe_atoi(argv[1], &count))
			return -1;
	} else if (strcmp("static", argv[0]) == 0) {
		if (safe_atoi(argv[1], &state))
			return -1;
	} else {
		return -1;
	}

	if (blink) {
		c->tone_code = 2; // default tone
		c->on_count = 5;
		c->off_count = 5;
		c->rep_count = count;
	} else {
		c->tone_code = (state == 0) ? 0 : 2; // no tone or default
		c->on_count = 5;
		c->off_count = 0;
		c->rep_count = 0;
	}

	return 0;
}

int handle_cmd_output(int argc, char *argv[], struct osdp_cmd_output *c)
{
	int out_no, state;

	if (argc != 2) {
		printf("Usage: output <output_number> <state>\n");
		return -2;
	}

	if (safe_atoi(argv[0], &out_no))
		return -1;
	if (safe_atoi(argv[1], &state))
		return -1;

	c->output_no = out_no;
	c->control_code = (state == 0) ? 1 : 2;
	c->tmr_count = 0;

	return 0;
}

int handle_cmd_text(int argc, char *argv[], struct osdp_cmd_text *c)
{
	int len;

	if (argc != 1) {
		printf("Usage: text <string>\n");
		return -2;
	}

	len = strlen(argv[0]);
	if (len > 32)
		return -1;

	c->cmd = 1;
	c->length = len;
	strncpy((char *)c->data, argv[0], len);

	return 0;
}

int handle_cmd_comset(int argc, char *argv[], struct osdp_cmd_comset *c)
{
	int address, baud;

	if (argc != 2) {
		printf("Usage: output <address> <baud_rate>\n");
		return -2;
	}

	if (safe_atoi(argv[0], &address))
		return -1;
	if (safe_atoi(argv[1], &baud))
		return -1;

	if (address <= 0 || address >= 126 ||
	    (baud != 9600 && baud != 38400 && baud != 115200))
		return -1;

	c->addr = (uint8_t)address;
	c->baud = (uint32_t)baud;

	return 0;
}

int cmd_handler_send(int argc, char *argv[], void *data)
{
	int offset, ret = -127;
	struct config_s *c = data;
	struct osdpctl_cmd mq_cmd;

	if (argc < 1) {
		printf ("Error: must pass a config file\n");
		return -1;
	}
	if (argc < 3) {
		printf("Error: PD offset/command is missing\n");
		goto print_usage;
	}

	if (safe_atoi(argv[1], &offset)) {
		printf("Error: Invalid PD offset");
		return -1;
	}

	config_parse(argv[0], c);

	memset(&mq_cmd.cmd, 0, sizeof(union osdp_cmd));
	mq_cmd.offset = offset;

	if (strcmp("led", argv[2]) == 0) {
		mq_cmd.id = OSDPCTL_CP_CMD_LED;
		ret = handle_cmd_led(argc-3, argv+3, &mq_cmd.cmd.led);
	}
	else if (strcmp("buzzer", argv[2]) == 0) {
		mq_cmd.id = OSDPCTL_CP_CMD_BUZZER;
		ret = handle_cmd_buzzer(argc-3, argv+3, &mq_cmd.cmd.buzzer);
	}
	else if (strcmp("output", argv[2]) == 0) {
		mq_cmd.id = OSDPCTL_CP_CMD_OUTPUT;
		ret = handle_cmd_output(argc-3, argv+3, &mq_cmd.cmd.output);
	}
	else if (strcmp("text", argv[2]) == 0) {
		mq_cmd.id = OSDPCTL_CP_CMD_TEXT;
		ret = handle_cmd_text(argc-3, argv+3, &mq_cmd.cmd.text);
	}
	else if (strcmp("comset", argv[2]) == 0) {
		mq_cmd.id = OSDPCTL_CP_CMD_COMSET;
		ret = handle_cmd_comset(argc-3, argv+3, &mq_cmd.cmd.comset);
	} else {
		printf("Error: unkown command %s\n", argv[2]);
		goto print_usage;
	}

	if (ret != 0) {
		if (ret == -1)
			printf("Error: incorrect command structure/grammer\n");
		return -1;
	}

	if (msgq_send_command(c, &mq_cmd)) {
		printf("Error: failed to send command\n");
		return -1;
	}

	return 0;

print_usage:
	printf("\nUsage: <PD> <COMMAND> [ARGS..]\n\n");
	printf("COMMANDS:\n\t"
		"led\n\tbuzzer\n\toutput\n\ttext\n\tcomset\n\n");
	return -1;
}