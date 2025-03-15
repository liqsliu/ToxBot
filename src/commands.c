/*  commands.c
 *
 *
 *  Copyright (C) 2021 toxbot All Rights Reserved.
 *
 *  This file is part of toxbot.
 *
 *  toxbot is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  toxbot is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with toxbot. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <tox/tox.h>
#include <tox/toxav.h>

#include "toxbot.h"
#include "misc.h"
#include "groupchats.h"
#include "log.h"

#define MAX_COMMAND_LENGTH TOX_MAX_MESSAGE_LENGTH
#define MAX_NUM_ARGS 4

extern struct Tox_Bot Tox_Bot;

/** static struct { */
/**     const char *name; */
/**     void (*func)(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH]); */
/** } commands[] = { */
struct CF {
    const char *name;
    void (*func)(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH]);
    bool admin_only;
};
// add by liqsliu
/* #define MAX_NUM_ARGS 16 //测试结果: 重复定义会以第二次定义的为准 */
#define MAX_GROUPS 64
extern uint32_t PUBLIC_GROUP_NUM;
extern uint32_t MY_NUM;
extern bool joined_group;

// add by liqsliu

static void authent_failed(Tox *m, uint32_t friendnumber)
{
    const char *outmsg = "You do not have permission to use this command.";
    tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
}

static void send_error(Tox *m, uint32_t friendnumber, const char *message, int err)
{
    char outmsg[TOX_MAX_MESSAGE_LENGTH];
    snprintf(outmsg, sizeof(outmsg), "%s (error %d)", message, err);
    tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
}

static void cmd_default(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    const char *outmsg = NULL;

    if (!friend_is_master(m, friendnumber)) {
        authent_failed(m, friendnumber);
        return;
    }

    if (argc < 1) {
        outmsg = "Error: Room number required";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    int groupnum = atoi(argv[1]);

    if ((groupnum == 0 && strcmp(argv[1], "0")) || groupnum < 0) {
        outmsg = "Error: Invalid room number";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    Tox_Bot.default_groupnum = groupnum;

    char msg[MAX_COMMAND_LENGTH];
    snprintf(msg, sizeof(msg), "Default room number set to %d", groupnum);
    tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) msg, strlen(msg), NULL);

    char name[TOX_MAX_NAME_LENGTH];
    tox_friend_get_name(m, friendnumber, (uint8_t *) name, NULL);
    size_t len = tox_friend_get_name_size(m, friendnumber, NULL);
    name[len] = '\0';

    log_timestamp("Default room number set to %d by %s", groupnum, name);
}

static void cmd_gmessage(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    const char *outmsg = NULL;

    if (!friend_is_master(m, friendnumber)) {
        authent_failed(m, friendnumber);
        return;
    }

    if (argc < 1) {
        outmsg = "Error: Group number required";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    if (argc < 2) {
        outmsg = "Error: Message required";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    int groupnum = atoi(argv[1]);

    if (groupnum == 0 && strcmp(argv[1], "0")) {
        outmsg = "Error: Invalid group number";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    if (group_index(groupnum) == -1) {
        outmsg = "Error: Invalid group number";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    if (argv[2][0] != '\"') {
        outmsg = "Error: Message must be enclosed in quotes";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    /* remove opening and closing quotes */
    char msg[MAX_COMMAND_LENGTH];
    snprintf(msg, sizeof(msg), "%s", &argv[2][1]);
    int len = strlen(msg) - 1;
    msg[len] = '\0';

    TOX_ERR_CONFERENCE_SEND_MESSAGE err;

    if (!tox_conference_send_message(m, groupnum, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) msg, strlen(msg), &err)) {
        outmsg = "Error: Failed to send message.";
        send_error(m, friendnumber, outmsg, err);
        return;
    }

    char name[TOX_MAX_NAME_LENGTH];
    tox_friend_get_name(m, friendnumber, (uint8_t *) name, NULL);
    size_t nlen = tox_friend_get_name_size(m, friendnumber, NULL);
    name[nlen] = '\0';

    outmsg = "Message sent.";
    tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
    log_timestamp("<%s> message to group %d: %s", name, groupnum, msg);
}

static void cmd_group(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    const char *outmsg = NULL;

    if (argc < 1) {
        outmsg = "Please specify the group type: audio or text";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    uint8_t type = TOX_CONFERENCE_TYPE_AV ? !strcasecmp(argv[1], "audio") : TOX_CONFERENCE_TYPE_TEXT;

    char name[TOX_MAX_NAME_LENGTH];
    tox_friend_get_name(m, friendnumber, (uint8_t *) name, NULL);
    size_t len = tox_friend_get_name_size(m, friendnumber, NULL);
    name[len] = '\0';

    int groupnum = -1;

    if (type == TOX_CONFERENCE_TYPE_TEXT) {
        TOX_ERR_CONFERENCE_NEW err;
        groupnum = tox_conference_new(m, &err);

        if (err != TOX_ERR_CONFERENCE_NEW_OK) {
            log_error_timestamp(err, "Group chat creation by %s failed to initialize", name);
            outmsg = "Group chat instance failed to initialize.";
            tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
            return;
        }
    } else if (type == TOX_CONFERENCE_TYPE_AV) {
        groupnum = toxav_add_av_groupchat(m, NULL, NULL);

        if (groupnum == -1) {
            log_error_timestamp(-1, "Group chat creation by %s failed to initialize", name);
            outmsg = "Group chat instance failed to initialize.";
            tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
            return;
        }
    }

    const char *password = argc >= 2 ? argv[2] : NULL;

    if (password && strlen(argv[2]) >= MAX_PASSWORD_SIZE) {
        log_error_timestamp(-1, "Group chat creation by %s failed: Password too long", name);
        outmsg = "Group chat instance failed to initialize: Password too long";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    if (group_add(groupnum, type, password) == -1) {
        log_error_timestamp(-1, "Group chat creation by %s failed", name);
        outmsg = "Group chat creation failed";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        tox_conference_delete(m, groupnum, NULL);
        return;
    }

    const char *pw = password ? " (Password protected)" : "";
    log_timestamp("Group chat %d created by %s%s", groupnum, name, pw);

    char msg[MAX_COMMAND_LENGTH];
    snprintf(msg, sizeof(msg), "Group chat %d created%s", groupnum, pw);
    tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) msg, strlen(msg), NULL);
}


static void sendme(Tox *m, const char *outmsg)
{
    if (MY_NUM != UINT32_MAX) {
        tox_friend_send_message(m, MY_NUM, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
    } else {
        log_timestamp("MY_NUM is not set");
    }
}

static void cmd_help(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    log_timestamp("argc: %d", argc+1);
    printf("argv:");
    int i;
    for (i=0; i<argc+1; ++i) {
        printf(" \"%s\"", argv[i]);
    }
    printf("\n");

    const char *outmsg = NULL;

    if (argc == 0) {
        outmsg = ".info : Print my current status and list active group chats";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);

        outmsg = ".id : Print my Tox ID";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);

        outmsg = ".invite : Request invite to default group chat";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);

        outmsg = ".invite <n> <p> : Request invite to group chat n (with password p if protected)";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);

        outmsg = ".group <type> <pass> : Creates a new groupchat with type: text | audio (optional password)";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);

        if (friend_is_master(m, friendnumber)) {
            outmsg = "For a list of master commands see the commands.txt file";
            tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        }
        return;
    }
    if (argc == 1) {
        if (strcmp(argv[1], "admin") == 0) {
            log_timestamp("opening txt...");
            FILE *fp = NULL;
            char path[1024]=SH_PATH;
            strcat(path, "/commands.txt");
            if (file_exists(path) != true)
            {
                outmsg = "not found commands.txt file";
                tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
                return;
            }
            fp = fopen(path, "r");
            if (fp == NULL) {
                fprintf(stderr, "Warning: failed to read '%s' file\n", path);
                return;
            }
            char line[TOX_MAX_MESSAGE_LENGTH];
            line[0] = '\0';
            log_timestamp("reading txt...");
            while (fgets(line, TOX_MAX_MESSAGE_LENGTH, fp)) {
                /** log_timestamp("got: %s", line); */
                outmsg = line;
                tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
                line[0] = '\0';
            }
            fclose(fp);
            return;
        }
    }
    outmsg = "send: .help";
    tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
    return;
}
int String2Int(char *str)//字符串转数字
{
    char flag = '+';//指示结果是否带符号
    long res = 0;
    if(*str=='-')//字符串带负号
    {
        ++str;//指向下一个字符
        flag = '-';//将标志设为负号
    }
    sscanf(str, "%ld", &res);
    if(flag == '-')
    {
        res = -res;
    }
    return (int)res;
}
static void cmd_exit(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    if (argc == 0) {
        sendme(m, ".exit number");
        return;
    }
    int gn = String2Int(argv[1]);
    if(tox_group_is_connected(m, gn, NULL) == true)
    {
        log_timestamp("connected, really?");
        sendme(m, "connected?");
    }
    else
        sendme(m, "not connect");
    if (tox_group_disconnect(m, gn, NULL) == true)
    {
        log_timestamp("disconnected");
        sendme(m, "ok");
    }
    else
        sendme(m, "failed");
}
int save_chat_ids(char *chat_ids)
{
    char * path="group_chat_ids";
    if (path == NULL) {
        goto on_error;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        goto on_error;
    }
    if (fwrite(chat_ids, strlen(chat_ids), 1, fp) != 1) {
        fclose(fp);
        goto on_error;
    }
    fclose(fp);
    log_timestamp("saved chat_ids");
    return 0;

on_error:
    log_error_timestamp(-1, "Warning: save failed");
    return -1;
}

static void cmd_save(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    int n = tox_group_get_number_groups(m);
    log_timestamp("现在群数量: %d", n);
    if (n == 0)
    {
        /* sendme(m, "no connected group"); */
        const char * outmsg="no connected group";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
    }
    else
    {
        char outmsg[TOX_MAX_MESSAGE_LENGTH]="found: ";
        sprintf(outmsg+strlen(outmsg), "%d", n);
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        outmsg[0] = '\0';
        int i, j;
        char chat_ids[TOX_GROUP_CHAT_ID_SIZE*MAX_GROUPS*2+MAX_GROUPS+1]={0};
        for (i=0; i<n; ++i)
        {
            sprintf(outmsg+strlen(outmsg), "%d", i);

            char public_key[TOX_PUBLIC_KEY_SIZE];
            bool res = tox_group_get_chat_id(m, i, (uint8_t *)public_key, NULL);
            if (res != true) {
                sprintf(outmsg+strlen(outmsg), "无法获取chat_id: %d", i);
                tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
                outmsg[0] = '\0';
                break;
            }
            if (strlen(chat_ids) != 0)
                chat_ids[strlen(chat_ids)] = '\n';
            for (j=0; j<sizeof(public_key); j++)
            {
                /* printf("%hhX", public_key[i]); */
                sprintf(chat_ids+strlen(chat_ids), "%hhX", public_key[j]);
            }
            if (i >= MAX_GROUPS) {
                sprintf(outmsg+strlen(outmsg), "群数量已达到上限: %d/%d", MAX_GROUPS, n);
                tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
                break;
            }

        }
        chat_ids[strlen(chat_ids)+1] = '\0';
        chat_ids[strlen(chat_ids)] = '\n';
        save_chat_ids(chat_ids);
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) "ok", strlen(outmsg), NULL);
    }

}


static void cmd_list(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    int n = tox_group_get_number_groups(m);
    log_timestamp("现在public群数量: %d", n);
    if (n == 0)
    {
        /* sendme(m, "no connected group"); */
        const char * outmsg="no connected group";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
    }
    else
    {
        char outmsg[TOX_MAX_MESSAGE_LENGTH]="connected public groups: ";
        sprintf(outmsg+strlen(outmsg), "%d", n);
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        outmsg[0] = '\0';

        char title[TOX_MAX_NAME_LENGTH];
        int len;
        int i, j;
        for (i=0; i<n; ++i)
        {
            sprintf(outmsg+strlen(outmsg), "%d", i);

            tox_group_get_name(m, i, (uint8_t *) title, NULL);
            len = tox_group_get_name_size(m, i, NULL);
            title[len] = '\0';
            log_timestamp("title: %d. %s", i, title);
            sprintf(outmsg+strlen(outmsg), " %s ", title);

            char public_key[TOX_PUBLIC_KEY_SIZE];
            bool res = tox_group_get_chat_id(m, i, (uint8_t *)public_key, NULL);
            if (res != true) {
                sprintf(outmsg+strlen(outmsg), "无法获取chat_id: %d", i);
                /* sendme(m, outmsg); */
                tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
                outmsg[0] = '\0';
                break;
            }
            for (j=0; j<sizeof(public_key); j++)
            {
                /* printf("%hhX", public_key[i]); */
                sprintf(outmsg+strlen(outmsg), "%hhX", public_key[j]);
            }
            tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
            outmsg[0] = '\0';
            if (i >= MAX_GROUPS) {
                sprintf(outmsg+strlen(outmsg), "群数量已达到上限: %d/%d", MAX_GROUPS, n);
                tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
                break;
            }

        }
    }
}
static void cmd_rejoin(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    if (argc == 0) {
        sendme(m, ".rejoin number");
        return;
    }
    int gn = String2Int(argv[1]);
    log_timestamp("group number: %d", gn);
    if(tox_group_is_connected(m, gn, NULL) == true)
    {
        log_timestamp("connected, really?");
        sendme(m, "connected?");
    }
    else
        sendme(m, "not connected");
    /* if (tox_group_disconnect(m, gn, NULL) == true) */
    /* { */
    /*     sendme(m, "disconnected"); */
    /* } */
    /* else */
    /*     sendme(m, "disconnected failed"); */
    /* sleep(1); */
    Tox_Err_Group_Reconnect  err;
    bool res = tox_group_reconnect(m, gn, &err);
    if (res == true && err == TOX_ERR_GROUP_RECONNECT_OK)
    {
        sendme(m, "reconnect ok");
    } else {
        sendme(m, "reconnect failed");
    }
    /* sleep(1); */
    int n = tox_group_get_number_groups(m);
    log_timestamp("现在群数量: %d", n);

}
static void cmd_join(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    char * chat_id;
    if (argc == 0) {
        chat_id = CHAT_ID;
    } else {
        chat_id = argv[1];
    }
    if (strlen(chat_id) < 32) {
        char *outmsg="wrong chat_id";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }
    if (join_public_group_by_chat_id(m, chat_id) == 0) {
        char *outmsg="ok";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
    } else {
        char *outmsg="failed";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
    }
}
static void cmd_init(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    /* if (!friend_is_master(m, friendnumber)) { */
    /*     authent_failed(m, friendnumber); */
    /*     log_timestamp("已忽略命令: %d %s", friendnumber, argv[0]); */
    /*     return; */
    /* } */
    /** if (PUBLIC_GROUP_NUM == UINT32_MAX) */
    join_public_group(m);
    /** log_timestamp("join: %s", CHAT_ID); */
    /** } else { */
    /**     rejoin_public_group(m, PUBLIC_GROUP_NUM); */
    /**     log_timestamp("rejoin: %d", PUBLIC_GROUP_NUM); */
    const char *outmsg =NULL;
    if (joined_group == true)
        outmsg = "ok";
    else
        outmsg = "failed";
    tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
}

static void cmd_id(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    char outmsg[TOX_ADDRESS_SIZE * 2 + 1];
    char address[TOX_ADDRESS_SIZE];
    tox_self_get_address(m, (uint8_t *) address);

    for (size_t i = 0; i < TOX_ADDRESS_SIZE; ++i) {
        char d[3];
        sprintf(d, "%02X", address[i] & 0xff);
        memcpy(outmsg + i * 2, d, 2);
    }

    outmsg[TOX_ADDRESS_SIZE * 2] = '\0';
    tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
}

static void cmd_info(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    char outmsg[MAX_COMMAND_LENGTH];
    char timestr[64];

    time_t curtime = get_time();
    get_elapsed_time_str(timestr, sizeof(timestr), curtime - Tox_Bot.start_time);
    snprintf(outmsg, sizeof(outmsg), "Uptime: %s", timestr);
    tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);

    uint32_t numfriends = tox_self_get_friend_list_size(m);
    snprintf(outmsg, sizeof(outmsg), "Friends: %d (%d online)", numfriends, Tox_Bot.num_online_friends);
    tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);

    snprintf(outmsg, sizeof(outmsg), "Inactive friends are purged after %"PRIu64" days",
             Tox_Bot.inactive_limit / SECONDS_IN_DAY);
    tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);

    /* List active group chats and number of peers in each */
    size_t num_chats = tox_conference_get_chatlist_size(m);

    if (num_chats == 0) {
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) "No active groupchats", strlen("No active groupchats"), NULL);
        return;
    }

    uint32_t groupchat_list[num_chats];

    tox_conference_get_chatlist(m, groupchat_list);

    for (size_t i = 0; i < num_chats; ++i) {
        TOX_ERR_CONFERENCE_PEER_QUERY err;
        uint32_t groupnum = groupchat_list[i];
        uint32_t num_peers = tox_conference_peer_count(m, groupnum, &err);

        if (err == TOX_ERR_CONFERENCE_PEER_QUERY_OK) {
            int idx = group_index(groupnum);
            const char *title = Tox_Bot.g_chats[idx].title_len
                                ? Tox_Bot.g_chats[idx].title : "None";
            const char *type = tox_conference_get_type(m, groupnum, NULL) == TOX_CONFERENCE_TYPE_AV ? "Audio" : "Text";
            snprintf(outmsg, sizeof(outmsg), "Group %d | %s | peers: %d | Title: %s", groupnum, type,
                     num_peers, title);
            tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        }
    }
    cmd_list(m, friendnumber, argc, argv);
}

static void cmd_invite(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    const char *outmsg = NULL;
    int groupnum = Tox_Bot.default_groupnum;

    if (argc >= 1) {
        groupnum = atoi(argv[1]);

        if (groupnum == 0 && strcmp(argv[1], "0")) {
            outmsg = "Error: Invalid group number";
            tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
            return;
        }
    }

    int idx = group_index(groupnum);

    if (idx == -1) {
        outmsg = "Group doesn't exist.";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    int has_pass = Tox_Bot.g_chats[idx].has_pass;

    char name[TOX_MAX_NAME_LENGTH];
    tox_friend_get_name(m, friendnumber, (uint8_t *) name, NULL);
    size_t len = tox_friend_get_name_size(m, friendnumber, NULL);
    name[len] = '\0';

    const char *passwd = NULL;

    if (argc >= 2) {
        passwd = argv[2];
    }

    if (has_pass && (!passwd || strcmp(argv[2], Tox_Bot.g_chats[idx].password) != 0)) {
        log_error_timestamp(-1, "Failed to invite %s to group %d (invalid password)", name, groupnum);
        outmsg = "Invalid password.";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    TOX_ERR_CONFERENCE_INVITE err;

    if (!tox_conference_invite(m, friendnumber, groupnum, &err)) {
        log_error_timestamp(err, "Failed to invite %s to group %d", name, groupnum);
        outmsg = "Invite failed";
        send_error(m, friendnumber, outmsg, err);
        return;
    } else
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen("ok"), NULL);

    log_timestamp("Invited %s to group %d", name, groupnum);
}

static void cmd_leave(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    const char *outmsg = NULL;

    if (!friend_is_master(m, friendnumber)) {
        authent_failed(m, friendnumber);
        return;
    }

    if (argc < 1) {
        outmsg = "Error: Group number required";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    int groupnum = atoi(argv[1]);

    if (groupnum == 0 && strcmp(argv[1], "0")) {
        outmsg = "Error: Invalid group number";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    if (!tox_conference_delete(m, groupnum, NULL)) {
        outmsg = "Error: Invalid group number";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    char msg[MAX_COMMAND_LENGTH];

    char name[TOX_MAX_NAME_LENGTH];
    tox_friend_get_name(m, friendnumber, (uint8_t *) name, NULL);
    size_t len = tox_friend_get_name_size(m, friendnumber, NULL);
    name[len] = '\0';

    group_leave(groupnum);

    log_timestamp("Left group %d (%s)", groupnum, name);
    snprintf(msg, sizeof(msg), "Left group %d", groupnum);
    tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) msg, strlen(msg), NULL);
}

static void cmd_master(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    const char *outmsg = NULL;

    if (!friend_is_master(m, friendnumber)) {
        authent_failed(m, friendnumber);
        return;
    }

    if (argc < 1) {
        outmsg = "Error: Tox ID required";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    const char *id = argv[1];

    if (strlen(id) != TOX_ADDRESS_SIZE * 2) {
        outmsg = "Error: Invalid Tox ID";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    FILE *fp = fopen(MASTERLIST_FILE, "a");

    if (fp == NULL) {
        outmsg = "Error: could not find masterkeys file";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    fprintf(fp, "%s\n", id);
    fclose(fp);

    char name[TOX_MAX_NAME_LENGTH];
    tox_friend_get_name(m, friendnumber, (uint8_t *) name, NULL);
    size_t len = tox_friend_get_name_size(m, friendnumber, NULL);
    name[len] = '\0';

    log_timestamp("%s added master: %s", name, id);
    outmsg = "ID added to masterkeys list";
    tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
}

static void cmd_name(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    const char *outmsg = NULL;

    if (!friend_is_master(m, friendnumber)) {
        authent_failed(m, friendnumber);
        return;
    }

    if (argc < 1) {
        outmsg = "Error: Name required";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }
    if (strlen(argv[1]) > TOX_MAX_NAME_LENGTH-1) {
        outmsg = "Error: Name is too long";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    char name[TOX_MAX_NAME_LENGTH];
    int len = 0;

    if (argv[1][0] == '\"') {    /* remove opening and closing quotes */
        /* snprintf(name, sizeof(name), "%s", &argv[1][1]); */
        strcpy(name, &argv[1][1]);
        len = strlen(name) - 1;
        name[len] = '\0';
    } else {
        /* snprintf(name, sizeof(name), "%s", argv[1]); */
        strcpy(name, argv[1]);
        len = strlen(name);
        /* name[len] = '\0'; */
    }

    tox_self_set_name(m, (uint8_t *) name, (uint16_t) len, NULL);

    char m_name[TOX_MAX_NAME_LENGTH];
    tox_friend_get_name(m, friendnumber, (uint8_t *) m_name, NULL);
    size_t nlen = tox_friend_get_name_size(m, friendnumber, NULL);
    m_name[nlen] = '\0';

    log_timestamp("%s set name to %s", m_name, name);
    save_data(m, DATA_FILE);
}

static void cmd_passwd(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    const char *outmsg = NULL;

    if (!friend_is_master(m, friendnumber)) {
        authent_failed(m, friendnumber);
        return;
    }

    if (argc < 1) {
        outmsg = "Error: group number required";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    int groupnum = atoi(argv[1]);

    if (groupnum == 0 && strcmp(argv[1], "0")) {
        outmsg = "Error: Invalid group number";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    int idx = group_index(groupnum);

    if (idx == -1) {
        outmsg = "Error: Invalid group number";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    char name[TOX_MAX_NAME_LENGTH];
    tox_friend_get_name(m, friendnumber, (uint8_t *) name, NULL);
    size_t nlen = tox_friend_get_name_size(m, friendnumber, NULL);
    name[nlen] = '\0';


    /* no password */
    if (argc < 2) {
        Tox_Bot.g_chats[idx].has_pass = false;
        memset(Tox_Bot.g_chats[idx].password, 0, MAX_PASSWORD_SIZE);

        outmsg = "No password set";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        log_timestamp("No password set for group %d by %s", groupnum, name);
        return;
    }

    if (strlen(argv[2]) >= MAX_PASSWORD_SIZE) {
        outmsg = "Password too long";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    Tox_Bot.g_chats[idx].has_pass = true;
    snprintf(Tox_Bot.g_chats[idx].password, sizeof(Tox_Bot.g_chats[idx].password), "%s", argv[2]);

    outmsg = "Password set";
    tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
    log_timestamp("Password for group %d set by %s", groupnum, name);

}

static void cmd_purge(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    const char *outmsg = NULL;

    if (!friend_is_master(m, friendnumber)) {
        authent_failed(m, friendnumber);
        return;
    }

    if (argc < 1) {
        outmsg = "Error: number > 0 required";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    uint64_t days = (uint64_t) atoi(argv[1]);

    if (days <= 0) {
        outmsg = "Error: number > 0 required";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    uint64_t seconds = days * SECONDS_IN_DAY;
    Tox_Bot.inactive_limit = seconds;

    char name[TOX_MAX_NAME_LENGTH];
    tox_friend_get_name(m, friendnumber, (uint8_t *) name, NULL);
    size_t nlen = tox_friend_get_name_size(m, friendnumber, NULL);
    name[nlen] = '\0';

    char msg[MAX_COMMAND_LENGTH];
    snprintf(msg, sizeof(msg), "Purge time set to %"PRIu64" days", days);
    tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) msg, strlen(msg), NULL);

    log_timestamp("Purge time set to %"PRIu64" days by %s", days, name);
}

static void cmd_status(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    const char *outmsg = NULL;

    if (!friend_is_master(m, friendnumber)) {
        authent_failed(m, friendnumber);
        return;
    }

    if (argc < 1) {
        outmsg = "Error: status required";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    TOX_USER_STATUS type;
    const char *status = argv[1];

    if (strcasecmp(status, "online") == 0) {
        type = TOX_USER_STATUS_NONE;
    } else if (strcasecmp(status, "away") == 0) {
        type = TOX_USER_STATUS_AWAY;
    } else if (strcasecmp(status, "busy") == 0) {
        type = TOX_USER_STATUS_BUSY;
    } else {
        outmsg = "Invalid status. Valid statuses are: online, busy and away.";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    tox_self_set_status(m, type);

    char name[TOX_MAX_NAME_LENGTH];
    tox_friend_get_name(m, friendnumber, (uint8_t *) name, NULL);
    size_t nlen = tox_friend_get_name_size(m, friendnumber, NULL);
    name[nlen] = '\0';

    log_timestamp("%s set status to %s", name, status);
    save_data(m, DATA_FILE);
}

static void cmd_statusmessage(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    const char *outmsg = NULL;

    if (!friend_is_master(m, friendnumber)) {
        authent_failed(m, friendnumber);
        return;
    }

    if (argc < 1) {
        outmsg = "Error: message required";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    if (argv[1][0] != '\"') {
        outmsg = "Error: message must be enclosed in quotes";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    /* remove opening and closing quotes */
    char msg[MAX_COMMAND_LENGTH];
    snprintf(msg, sizeof(msg), "%s", &argv[1][1]);
    int len = strlen(msg) - 1;
    msg[len] = '\0';

    tox_self_set_status_message(m, (uint8_t *) msg, len, NULL);

    char name[TOX_MAX_NAME_LENGTH];
    tox_friend_get_name(m, friendnumber, (uint8_t *) name, NULL);
    size_t nlen = tox_friend_get_name_size(m, friendnumber, NULL);
    name[nlen] = '\0';

    log_timestamp("%s set status message to \"%s\"", name, msg);
    save_data(m, DATA_FILE);
}

static void cmd_title_set(Tox *m, uint32_t friendnumber, int argc, char (*argv)[MAX_COMMAND_LENGTH])
{
    const char *outmsg = NULL;

    if (!friend_is_master(m, friendnumber)) {
        authent_failed(m, friendnumber);
        return;
    }

    if (argc < 2) {
        outmsg = "Error: Two arguments are required";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    if (argv[2][0] != '\"') {
        outmsg = "Error: title must be enclosed in quotes";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    int groupnum = atoi(argv[1]);

    if (groupnum == 0 && strcmp(argv[1], "0")) {
        outmsg = "Error: Invalid group number";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
        return;
    }

    /* remove opening and closing quotes */
    char title[MAX_COMMAND_LENGTH];
    snprintf(title, sizeof(title), "%s", &argv[2][1]);
    int len = strlen(title) - 1;
    title[len] = '\0';

    char name[TOX_MAX_NAME_LENGTH];
    tox_friend_get_name(m, friendnumber, (uint8_t *) name, NULL);
    size_t nlen = tox_friend_get_name_size(m, friendnumber, NULL);
    name[nlen] = '\0';

    TOX_ERR_CONFERENCE_TITLE err;

    if (!tox_conference_set_title(m, groupnum, (uint8_t *) title, len, &err)) {
        log_error_timestamp(err, "%s failed to set the title '%s' for group %d", name, title, groupnum);
        outmsg = "Failed to set title. This may be caused by an invalid group number or an empty room";
        send_error(m, friendnumber, outmsg, err);
        return;
    }

    int idx = group_index(groupnum);
    memcpy(Tox_Bot.g_chats[idx].title, title, len + 1);
    Tox_Bot.g_chats[idx].title_len = len;

    outmsg = "Group title set";
    tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
    log_timestamp("%s set group %d title to %s", name, groupnum, title);
}




/* Parses input command and puts args into arg array.
   Returns number of arguments on success, -1 on failure. */
/* static int parse_command(const char *input, char (*args)[MAX_COMMAND_LENGTH]) */
/* { */
/*     char *cmd = strdup(input); */
/*  */
/*     if (cmd == NULL) { */
/*         exit(EXIT_FAILURE); */
/*     } */
/*  */
/*     int num_args = 0; */
/*     int i = 0;    [> index of last char in an argument <] */
/*  */
/*     [> characters wrapped in double quotes count as one arg <] */
/*     while (num_args < MAX_NUM_ARGS) { */
/*         int qt_ofst = 0;    [> set to 1 to offset index for quote char at end of arg <] */
/*  */
/*         if (*cmd == '\"') { */
/*             qt_ofst = 1; */
/*             i = char_find(1, cmd, '\"'); */
/*  */
/*             if (cmd[i] == '\0') { */
/*                 free(cmd); */
/*                 return -1; */
/*             } */
/*         } else { */
/*             i = char_find(0, cmd, ' '); */
/*         } */
/*  */
/*         memcpy(args[num_args], cmd, i + qt_ofst); */
/*         args[num_args++][i + qt_ofst] = '\0'; */
/*  */
/*         if (cmd[i] == '\0') {  [> no more args <] */
/*             break; */
/*         } */
/*  */
/*         char tmp[MAX_COMMAND_LENGTH]; */
/*         snprintf(tmp, sizeof(tmp), "%s", &cmd[i + 1]); */
/*         strcpy(cmd, tmp);    [> tmp will always fit inside cmd <] */
/*     } */
/*  */
/*     free(cmd); */
/*     return num_args; */
/* } */
/** int char_find(char *cmd, int qq_n, char qq) */
/** { */
/**     char c = *cmd; */
/**     if (c == '\"' || c == '\'' ) */
/**     { */
/**         if (c == qq) */
/**         { */
/**             qq_n--; */
/**         } else { */
/**             qq_n++; */
/**         } */
/**     } */
/** } */

static int my_parse_command(const char *input, char (*args)[MAX_COMMAND_LENGTH])
{
    /** if (input[0] != '.') { */
    /**     return -1; */
    /** } */
    char *cmd = strdup(input);
    if (cmd == NULL) {
        exit(EXIT_FAILURE);
    }
    int num_args = 0;
    int i = 0;    /* index of last char in an argument */

    /* characters wrapped in double quotes count as one arg */
    int j, cmd_l, qq_n=0, jj;
    char c;
    bool need_escape;
    bool in_quote;
    bool need_escape2;
    /* char *p; */
    while (num_args < MAX_NUM_ARGS) {
        /* int qt_ofst = 0;    [> set to 1 to offset index for quote char at end of arg <] */
        if (need_escape2 == true) {
            /* p = args[num_args] + strlen(args[num_args]); */
            jj += j;
        } else {
            /* p = args[num_args]; */
            jj = 0;
        }

        cmd_l = strlen(cmd);
        in_quote=false;
        need_escape=false;
        need_escape2=false;
        for (j=0; j<cmd_l; ++j)
        {
            c = cmd[j];
            if (need_escape == true) {
                need_escape = false;
                if (c == ' ' && in_quote == false) {
                    need_escape2 = true;
                    --i;
                    break;
                }
            } else if (in_quote == true) {
                if (c == '"') {
                    in_quote=false;
                }
            } else {
                if (c == '\\') {
                    need_escape = true;
                } else if (c == '"') {
                    in_quote=true;
                } else if (c == ' ') {
                    break;
                }
            }
            /* if (c == qq) { */
            /*     if (c == qq1) { */
            /*         qq = qq2; */
            /*     } else if (c == qq2) { */
            /*         qq = qq1; */
            /*     } */
            /*     qq_n--; */
            /* } else if (c == qq1) { */
            /*     qq = qq1; */
            /*     qq_n++; */
            /* } else if (c == qq2) { */
            /*     qq = qq2; */
            /*     qq_n++; */
            /* } else if (qq_n == 0) { */
            /*     if (c == ' ') */
            /*         break; */
            /* } */
            
        }
        i = j;

        /** if (*cmd == '\"') { */
        /**     qt_ofst = 1; */
        /**     i = char_find(1, cmd, '\"'); */
        /**  */
        /**     if (cmd[i] == '\0') { */
        /**         free(cmd); */
        /**         return -1; */
        /**     } */
        /** } else { */
        /**     i = char_find(0, cmd, ' '); */
        /** } */

        /* memcpy(args[num_args], cmd, i + qt_ofst); */
        /* args[num_args++][i + qt_ofst] = '\0'; */

        /* memcpy(args[num_args], cmd, i); */
        /* args[num_args++][i] = '\0'; */
        memcpy(args[num_args]+jj, cmd, i);
        /* log_timestamp("num_args: %d\n", num_args); */
        /* log_timestamp("i: %d\n", i); */
        /* log_timestamp("j: %d\n", j); */
        /* log_timestamp("jj: %d\n", jj); */
        if (need_escape2 == true) {
            args[num_args][i] = ' ';
            ++i;
            args[num_args][jj+i] = '\0';
            log_timestamp("add tmp: |%s|", args[num_args]);
            ++i;
        } else {
            args[num_args][jj+i] = '\0';
            log_timestamp("add: |%s|", args[num_args]);
            ++num_args;
        }

        if (cmd[i] == '\0') {  /* no more args */
            break;
        }

        char tmp[MAX_COMMAND_LENGTH];
        snprintf(tmp, sizeof(tmp), "%s", &cmd[i + 1]);
        strcpy(cmd, tmp);    /* tmp will always fit inside cmd */
    }

    free(cmd);
    /** j = sizeof(args)/sizeof(args[0]); */
    log_timestamp("length: %d", num_args);
    printf("args:");
    for (i=0; i<num_args; ++i) {
        printf("\n> %s", args[i]);
    }
    printf("\n");
    return num_args; // 注意：执行命令函数时，该变量已经减一，可以看作是参数的个数，不包含命令本身。
}


/** void quick_sort_recursive_swap(int *x, int *y) { */
void quick_sort_recursive_swap(struct CF *x, struct CF *y) {
    struct CF t = *x;
    /** printf("swap: %d %d\n", *x, *y); */
    *x = *y;
    *y = t;
}
/** void quick_sort_recursive( int *start, int *end) { */
void quick_sort_recursive( struct CF *start, struct CF *end) {
    if (start >= end)
        return;
    /** int *left = start, *right = end - 1; */
    struct CF *left = start, *right = end - 1;
    while (left < right) {
        /** if (*right >= *end) */
        if (strcmp((*right).name, (*end).name) >= 0)
            --right;
        /** else if (*left < *end) */
        else if (strcmp((*left).name, (*end).name) < 0)
            ++left;
        else
        {
            quick_sort_recursive_swap(left, right);
            ++left;
            --right;
        }
    }
    /** printf("finally left: %d\n", left-start); */
    if (left > right)
        --left;
    /** else if (*left > *end) */
    else if (strcmp((*left).name, (*end).name) > 0)
        quick_sort_recursive_swap(left, end);

    quick_sort_recursive(start, left);
    quick_sort_recursive(left+1, end);
}
/** int quick_sort(int arr[], int len) { */
int quick_sort(struct CF arr[], int len) {
    quick_sort_recursive(arr, arr+len - 1);
    return 0;
}

struct CF commands[] = {
    { "default",          cmd_default, true       },
    { "group",            cmd_group         },
    { "gmessage",         cmd_gmessage, true      },
    { "help",             cmd_help          },
    { "id",               cmd_id            },
    { "info",             cmd_info          },
    { "invite",           cmd_invite        },
    { "leave",            cmd_leave, true         },
    { "master",           cmd_master, true        },
    { "name",             cmd_name, true          },
    { "passwd",           cmd_passwd, true        },
    { "purge",            cmd_purge, true         },
    { "status",           cmd_status, true        },
    { "statusmessage",    cmd_statusmessage, true },
    { "title",            cmd_title_set, true     },
    { "init",            cmd_init     },
    { "join",            cmd_join     },
    { "save",            cmd_save, true     },
    { "rejoin",            cmd_rejoin, true     },
    { "exit",            cmd_exit, true     },
    { "list",            cmd_list     },
    /** { NULL,               NULL              }, */
};


static const uint8_t commands_len = sizeof(commands)/sizeof(commands[0]);
static struct CF last_command;
void commands_init(void)
{
    static bool commands_sorted = false;
    if (commands_sorted == false)
    {
        log_timestamp("开始排序");
        quick_sort(commands, commands_len);
        commands_sorted = true;
        log_timestamp("排序结束");
        for (int i = 0; i < commands_len; i++) {
            printf("%d %s\n", i+1, commands[i].name);
        }
        printf("\n\ncommands_len: %d\n", commands_len);
    }
    last_command = commands[commands_len/2];
}
static int do_command(Tox *m, uint32_t friendnumber, int num_args, char (*args)[MAX_COMMAND_LENGTH])
{
    // static 可以做到保存上次查找到的位置，下次查找直接从该位置检查。如果命令相同，可以节省查找时间
    static int i=commands_len/2;
    uint8_t left=0, right=commands_len-1;
    int r;
    while (left <= right)
    {
        /* log_timestamp("check i: %d %s, %d %d", i, commands[i].name, left, right); */
        /* r = strcmp(args[0], commands[i].name); */
        r = strcmp(args[0], last_command.name);
        if (r == 0) {
            /* log_timestamp("hit cmd: %d %s", i, commands[i].name); */
            /* if (commands[i].admin_only == true) { */
            if (last_command.admin_only == true) {
                if (!friend_is_master(m, friendnumber)) {
                    authent_failed(m, friendnumber);
                    log_timestamp("已忽略命令: %d: %s", friendnumber, args[0]);
                    /* return -2; */
                    return 0;
                }
                /* if (MY_NUM == UINT32_MAX) { */
                MY_NUM = friendnumber;
            }
            /* (commands[i].func)(m, friendnumber, num_args - 1, args); */
            (last_command.func)(m, friendnumber, num_args - 1, args);
            return 0;
            
        }
        if (r > 0) {
            left = i+1;
            i = (right+i+1)/2;
        } else {
            right = i-1;
            i = (left+i-1)/2;
        }
        last_command = commands[i];
    }
    log_timestamp("not found: %s, %d %d", args[0], left, right);
    /** for (size_t i = 0; commands[i].name; ++i) { */
    /**     if (strcmp(args[0], commands[i].name) == 0) { */
    /**         (commands[i].func)(m, friendnumber, num_args - 1, args); */
    /**         return 0; */
    /**     } */
    /** } */

    return -1;
}


int execute(Tox *m, uint32_t friendnumber, const char *input, int length)
{
    if (length >= MAX_COMMAND_LENGTH) {
        return -1;
    }
    if (length < 2) {
        return -1;
    }
    /** if (input[0] == '.' && input[1] != '\0') { */
    if (input[0] == '.') {
        char args[MAX_NUM_ARGS][MAX_COMMAND_LENGTH];
        int num_args = my_parse_command(&input[1], args);
        if (num_args == -1) {
            return -1;
        }
        log_timestamp("run cmd: %s", input);
        return do_command(m, friendnumber, num_args, args);
    } else if (strcmp(input, "invite") == 0) {
        /** char args[MAX_NUM_ARGS][MAX_COMMAND_LENGTH]; */
        /** int num_args = parse_command(input, args); */
        /* char args[][8]={ */
        /* char * args[]={ */
        /* char * args[TOX_MAX_MESSAGE_LENGTH]={ */
        char args[][TOX_MAX_MESSAGE_LENGTH]={
        /* char args[MAX_NUM_ARGS][MAX_COMMAND_LENGTH]={ */
            "invite",
        };
        int num_args = 1;
        return do_command(m, friendnumber, num_args, args);
    /* } else if (strcmp(input, "help") == 0) { */
    /*     [> char * args[]={ <] */
    /*     [> char * args[TOX_MAX_MESSAGE_LENGTH]={ <] */
    /*     char args[][TOX_MAX_MESSAGE_LENGTH]={ */
    /*     [> char args[MAX_NUM_ARGS][MAX_COMMAND_LENGTH]={ <] */
    /*         "help", */
    /*     }; */
    /*     int num_args = 1; */
    /*     return do_command(m, friendnumber, num_args, args); */
    } else {
        return -1;
    }

    return -1;
}
