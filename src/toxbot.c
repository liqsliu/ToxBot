/*  toxbot.c
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
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>
#include <signal.h>
#include <getopt.h>
#include <stdarg.h>

#include <tox/tox.h>
#include <tox/toxav.h>

#include "misc.h"
#include "commands.h"
#include "toxbot.h"
#include "groupchats.h"
#include "log.h"

#define VERSION "0.1.2"

/* How often we attempt to purge inactive friends */
#define FRIEND_PURGE_INTERVAL (60 * 60)

/* How often we attempt to purge inactive groups */
#define GROUP_PURGE_INTERVAL (60 * 10)

/* How long we need to have had a stable connection before purging inactive groups */
#define GROUP_PURGE_CONNECT_TIMEOUT (60 * 60)

/* How often we attempt to bootstrap when not presently connected to the network */
#define BOOTSTRAP_INTERVAL 20

#define MAX_PORT_RANGE 65535

/* Name of data file prior to version 0.1.1 */
#define DATA_FILE_PRE_0_1_1 "toxbot_save"

volatile sig_atomic_t FLAG_EXIT = false;    /* set on SIGINT */

struct Tox_Bot Tox_Bot;

static struct Options {
    TOX_PROXY_TYPE    proxy_type;
    char      proxy_host[256];
    uint16_t  proxy_port;
    bool      disable_udp;
    bool      disable_lan;
    bool      force_ipv4;
} Options;

static void init_toxbot_state(void)
{
    Tox_Bot.start_time = get_time();
    Tox_Bot.last_connected = get_time();
    Tox_Bot.default_groupnum = 0;
    Tox_Bot.chats_idx = 0;
    Tox_Bot.num_online_friends = 0;

    /* 1 year default; anything lower should be explicitly set until we have a config file */
    Tox_Bot.inactive_limit = 31536000;
}

static void catch_SIGINT(int sig)
{
    FLAG_EXIT = true;
}

static void exit_toxbot(Tox *m)
{
    save_data(m, DATA_FILE);
    tox_kill(m);
    exit(EXIT_SUCCESS);
}

/* Returns true if friendnumber's Tox ID is in the masterkeys list. */
bool friend_is_master(Tox *m, uint32_t friendnumber)
{
    char public_key[TOX_PUBLIC_KEY_SIZE];

    if (tox_friend_get_public_key(m, friendnumber, (uint8_t *) public_key, NULL) == 0) {
        return false;
    }

    return file_contains_key(public_key, MASTERLIST_FILE) == 1;
}

/* Returns true if public_key is in the blockedkeys list. */
static bool public_key_is_blocked(const char *public_key)
{
    return file_contains_key(public_key, BLOCKLIST_FILE) == 1;
}

/* START CALLBACKS */
static void cb_self_connection_change(Tox *m, TOX_CONNECTION connection_status, void *userdata)
{
    switch (connection_status) {
        case TOX_CONNECTION_NONE:
            log_timestamp("Connection lost");
            Tox_Bot.last_bootstrap = get_time(); // usually we don't need to manually bootstrap if connection lost
            break;

        case TOX_CONNECTION_TCP:
            Tox_Bot.last_connected = get_time();
            log_timestamp("Connection established (TCP)");
            break;

        case TOX_CONNECTION_UDP:
            Tox_Bot.last_connected = get_time();
            log_timestamp("Connection established (UDP)");
            break;
    }
}

static void cb_friend_connection_change(Tox *m, uint32_t friendnumber, TOX_CONNECTION connection_status, void *userdata)
{
    Tox_Bot.num_online_friends = 0;

    size_t i, size = tox_self_get_friend_list_size(m);

    if (size == 0) {
        return;
    }

    uint32_t list[size];
    tox_self_get_friend_list(m, list);

    for (i = 0; i < size; ++i) {
        if (tox_friend_get_connection_status(m, list[i], NULL) != TOX_CONNECTION_NONE) {
            ++Tox_Bot.num_online_friends;
        }
    }
}

static void cb_friend_request(Tox *m, const uint8_t *public_key, const uint8_t *data, size_t length,
                              void *userdata)
{
    if (public_key_is_blocked((char *) public_key)) {
        return;
    }

    TOX_ERR_FRIEND_ADD err;
    tox_friend_add_norequest(m, public_key, &err);

    if (err != TOX_ERR_FRIEND_ADD_OK) {
        log_error_timestamp(err, "tox_friend_add_norequest failed");
    } else {
        log_timestamp("Accepted friend request");
    }

    save_data(m, DATA_FILE);
}

static void cb_friend_message(Tox *m, uint32_t friendnumber, TOX_MESSAGE_TYPE type, const uint8_t *string,
                              size_t length, void *userdata)
{
    if (type != TOX_MESSAGE_TYPE_NORMAL) {
        return;
    }

    char public_key[TOX_PUBLIC_KEY_SIZE];

    if (tox_friend_get_public_key(m, friendnumber, (uint8_t *) public_key, NULL) == 0) {
        return;
    }

    if (public_key_is_blocked(public_key)) {
        tox_friend_delete(m, friendnumber, NULL);
        return;
    }

    const char *outmsg;
    char message[TOX_MAX_MESSAGE_LENGTH];
    length = copy_tox_str(message, sizeof(message), (const char *) string, length);
    message[length] = '\0';

    if (length && execute(m, friendnumber, message, length) == -1) {
        outmsg = "Invalid command. Type help for a list of commands";
        tox_friend_send_message(m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) outmsg, strlen(outmsg), NULL);
    }
}

static void cb_group_invite(Tox *m, uint32_t friendnumber, TOX_CONFERENCE_TYPE type,
                            const uint8_t *cookie, size_t length, void *userdata)
{
    if (!friend_is_master(m, friendnumber)) {
        return;
    }

    char name[TOX_MAX_NAME_LENGTH];
    tox_friend_get_name(m, friendnumber, (uint8_t *) name, NULL);
    size_t len = tox_friend_get_name_size(m, friendnumber, NULL);
    name[len] = '\0';

    int groupnum = -1;

    if (type == TOX_CONFERENCE_TYPE_TEXT) {
        TOX_ERR_CONFERENCE_JOIN err;
        groupnum = tox_conference_join(m, friendnumber, cookie, length, &err);

        if (err != TOX_ERR_CONFERENCE_JOIN_OK) {
            goto on_error;
        }
    } else if (type == TOX_CONFERENCE_TYPE_AV) {
        groupnum = toxav_join_av_groupchat(m, friendnumber, cookie, length, NULL, NULL);

        if (groupnum == -1) {
            goto on_error;
        }
    }

    if (group_add(groupnum, type, NULL) == -1) {
        log_error_timestamp(-1, "Invite from %s failed (group_add failed)", name);
        tox_conference_delete(m, groupnum, NULL);
        return;
    }

    log_timestamp("Accepted groupchat invite from %s [%d]", name, groupnum);
    return;

on_error:
    log_error_timestamp(-1, "Invite from %s failed (core failure)", name);
}

static void cb_group_titlechange(Tox *m, uint32_t groupnumber, uint32_t peernumber, const uint8_t *title,
                                 size_t length, void *userdata)
{
    char message[TOX_MAX_MESSAGE_LENGTH];
    length = copy_tox_str(message, sizeof(message), (const char *) title, length);

    int idx = group_index(groupnumber);

    if (idx == -1) {
        return;
    }

    memcpy(Tox_Bot.g_chats[idx].title, message, length + 1);
    Tox_Bot.g_chats[idx].title_len = length;
}

// add by liqsliu
FILE *fd_gm;
char gmsg[TOX_MAX_MESSAGE_LENGTH];
char gmsgtmp[TOX_MAX_MESSAGE_LENGTH];
bool gm_lock=false;
#define BOT_NAME "bot"
#define GROUP_NAME "wtfipfs"
#define CHAT_ID "5CD71E298857CA3B502BE58383E3AF7122FCDE5BF46D5424192234DF83A76A66"
/** char *CHAT_ID="5CD71E298857CA3B502BE58383E3AF7122FCDE5BF46D5424192234DF83A76A66"; */
/** uint32_t PUBLIC_GROUP_NUM = UINT32_MAX; */
uint32_t PUBLIC_GROUP_NUM=0;
bool joined_group=false;
static void rejoin_public_group(Tox *m, Tox_Group_Number gn)
{
    sleep(3);
    if(tox_group_is_connected(m, gn, NULL) == true)
        log_timestamp("connected, really?");
    if(true)
    {
        if (tox_group_reconnect(m, gn, NULL) == true)
        {
            log_timestamp("2已加入public group，group number: %d", gn);
            char public_key[TOX_PUBLIC_KEY_SIZE];
            log_timestamp("%d", sizeof(public_key));
            /** bool res = tox_group_self_get_public_key(m, gn, (uint8_t *)public_key, NULL); */
            bool res = tox_group_get_chat_id(m, gn, (uint8_t *)public_key, NULL);
            log_timestamp("res: %x", res);
            log_timestamp("%d %X", sizeof(public_key), public_key);
            for (int i=0; i<sizeof(public_key); i++)
            {
                printf("%hhX", public_key[i]);
            }
            printf("\n");
            joined_group = true
        } else {
            log_timestamp("2failed，group number: %d", gn);
        }
    }
}

static void join_public_group(Tox *m)
{
    if (PUBLIC_GROUP_NUM == UINT32_MAX)
        return;
    if (joined_group == true)
        return;
    /** if (PUBLIC_GROUP_NUM + 10 > get_time()) */
    /** log_timestamp("开始加入: %d", PUBLIC_GROUP_NUM); */
    log_timestamp("开始加入: %s", CHAT_ID);
    log_timestamp("%s", (uint8_t *)CHAT_ID);
    uint8_t *key_bin = hex_string_to_bin(CHAT_ID);
    log_timestamp("%s", key_bin);
    /** PUBLIC_GROUP_NUM = tox_group_join(m, (uint8_t *)CHAT_ID, (uint8_t *)name, strlen(name), NULL, 0, NULL); */
    Tox_Err_Group_Join err;
    /** PUBLIC_GROUP_NUM = tox_group_join(m, (uint8_t *)CHAT_ID, (uint8_t *)BOT_NAME, strlen(BOT_NAME), NULL, 0, &err); */
    PUBLIC_GROUP_NUM = tox_group_join(m, (uint8_t *)key_bin, BOT_NAME, strlen(BOT_NAME), NULL, 0, &err);
    if (PUBLIC_GROUP_NUM == UINT32_MAX)
    {
        /** log_timestamp("加入失败，group number: %d", PUBLIC_GROUP_NUM); */
        log_timestamp("加入失败，group number: %d, %s", PUBLIC_GROUP_NUM, tox_err_group_join_to_string(err));
        /** PUBLIC_GROUP_NUM = get_time(); */
    
    } else
    {
        log_timestamp("已加入public group，group number: %d", PUBLIC_GROUP_NUM);
        rejoin_public_group(m, PUBLIC_GROUP_NUM);
    }
    free(key_bin);

}

static void cb_group_invite2(
    Tox *m, Tox_Friend_Number friend_number,
    const uint8_t invite_data[], size_t invite_data_length,
    const uint8_t group_name[], size_t group_name_length,
    void *user_data)
{
    log_timestamp("收到邀请: %d", friend_number);
    if (!friend_is_master(m, friend_number)) {
        log_timestamp("invite is not from master: %d", friend_number);
        return;
    }
    log_timestamp("开始加入: %d", PUBLIC_GROUP_NUM);
    log_timestamp("开始加入: %d %d", PUBLIC_GROUP_NUM, friend_number);
    /** log_timestamp("开始加入: %d %s", PUBLIC_GROUP_NUM, friend_number); */
    /** PUBLIC_GROUP_NUM = tox_group_invite_accept(m, friend_number, invite_data, invite_data_length, (uint8_t *)BOT_NAME, strlen(BOT_NAME), NULL, 0, NULL); */
    /** log_timestamp("group number: %d", PUBLIC_GROUP_NUM); */
    /** return; */
    Tox_Err_Group_Invite_Accept err;
    PUBLIC_GROUP_NUM = tox_group_invite_accept(m, friend_number, invite_data, invite_data_length, (uint8_t *)BOT_NAME, strlen(BOT_NAME), NULL, 0, &err);
    if (PUBLIC_GROUP_NUM == UINT32_MAX)
    {
        log_timestamp("加入失败，group number: %d, %s", PUBLIC_GROUP_NUM, tox_err_group_invite_accept_to_string(err));
    
    } else
    {
        log_timestamp("已加入public group，group number: %d", PUBLIC_GROUP_NUM);
        rejoin_public_group(m, PUBLIC_GROUP_NUM);
    }

}


#define SM_SH_PATH "bash /run/user/1000/bot/sm.sh \"$(cat <<EOF\n"
// # https://github.com/TokTok/c-toxcore/blob/81b1e4f6348124784088591c4fe9ab41e273031d/toxcore/tox.h#L2130
static void cb_conference_message(
    Tox *m, Tox_Conference_Number conference_number, Tox_Conference_Peer_Number peer_number,
    Tox_Message_Type type, const uint8_t message[], size_t length, void *user_data)
{
    log_timestamp("conference msg: %d %d %s", conference_number, peer_number, message);
    /** int idx = group_index(peer_number); //得到的是发信人在群成员列表的位置*/
    int idx = group_index(conference_number);
    if (idx == -1) {
        return;
    }

    char name[TOX_MAX_NAME_LENGTH];
    /** tox_group_peer_get_name(m, conference_number, peer_number, (uint8_t *) name, NULL); */
    tox_conference_peer_get_name(m, conference_number, peer_number, (uint8_t *) name, NULL);
    size_t len = tox_conference_peer_get_name_size(m, conference_number, peer_number, NULL);
    name[len] = '\0';

    char title[TOX_MAX_NAME_LENGTH];
    tox_conference_get_title(m, conference_number, (uint8_t *) title, NULL);
    len = tox_conference_get_title_size(m, conference_number, NULL);
    title[len] = '\0';


    if (idx == 0)
    {
        log_timestamp("群消息: %s [%s]: %s", title, name, message);
        if (strcmp(name, "bot") != 0)
        {
            /** char smsg[2048] = SM_SH_PATH; */
            char smsg[2048] = "bash /run/user/1000/bot/sm.sh \"$(cat <<EOF\n";
            strcat(smsg, name);
            strcat(smsg, "\nEOF\n)\" \"$(cat <<EOF\n");
            strcat(smsg, (char *)message);
            strcat(smsg, "\nEOF\n)\"");
            system(smsg);
        }
    } else {
        log_timestamp("忽略来自其他群的消息: %d %s [%s]: %s", idx, title, name, message);
    }
}
static void cb_group_message(
    Tox *m, Tox_Group_Number group_number, Tox_Group_Peer_Number peer_id, Tox_Message_Type message_type,
    const uint8_t message[], size_t message_length, Tox_Group_Message_Id message_id, void *user_data)
{
    log_timestamp("group msg: %d %d %s", group_number, peer_id, message);
    char name[TOX_MAX_NAME_LENGTH];
    tox_group_peer_get_name(m, group_number, peer_id, (uint8_t *) name, NULL);
    size_t len = tox_group_peer_get_name_size(m, group_number, peer_id, NULL);
    name[len] = '\0';

    char title[TOX_MAX_NAME_LENGTH];
    tox_group_get_name(m, group_number, (uint8_t *) title, NULL);
    len = tox_group_get_name_size(m, group_number, NULL);
    title[len] = '\0';

    if (group_number == PUBLIC_GROUP_NUM)
    {
        log_timestamp("ngc群消息: %s [%s]: %s", title, name, message);
        if (strcmp(name, "bot") != 0)
        {
            /** char smsg[2048] = SM_SH_PATH; */
            char smsg[2048] = "bash /run/user/1000/bot/sm.sh \"$(cat <<EOF\n";
            strcat(smsg, name);
            strcat(smsg, "\nEOF\n)\" \"$(cat <<EOF\n");
            strcat(smsg, (char *)message);
            strcat(smsg, "\nEOF\n)\"");
            system(smsg);
        }
    } else {
        log_timestamp("忽略来自其他ngc群的消息: %d %s [%s]: %s", group_number, title, name, message);
    }
}

static void send_to_tox(Tox *m, char *gmsg, size_t len)
{
    if (len >= 1)
    {
        log_timestamp("send msg to tox: %s", gmsg);

        /** Tox_Err_Group_Send_Message error; */
        // https://github.com/TokTok/c-toxcore/blob/172f279dc0647a538b30e62c96bab8bb1b0c8960/toxcore/tox.h#L4403

        log_timestamp("check...send msg to group: %s", gmsg);
        if (PUBLIC_GROUP_NUM != UINT32_MAX)
        {
            if (PUBLIC_GROUP_NUM < 99)
            {
              if (tox_group_send_message(m, PUBLIC_GROUP_NUM, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *)gmsg, len, NULL) != true)
              {
               log_timestamp("failed to send msg to group: %s", gmsg);
              } else {
                  log_timestamp("send msg to group: %s", gmsg);
              }

            }
        }

        TOX_ERR_CONFERENCE_SEND_MESSAGE err;
        //tox_conference_send_message(m, 0, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *)gmsg, strlen(gmsg), &err);
        tox_conference_send_message(m, 0, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *)gmsg, len, &err);
        if (err != TOX_ERR_CONFERENCE_SEND_MESSAGE_OK)
        {
    //			    char mymsg[2048] = "bash /run/user/1000/sm.sh \"ERR\" \"$(cat <<EOF\n";
            /** char mymsg[2048] = SM_SH_PATH; */
            char mymsg[2048] = "bash /run/user/1000/bot/sm.sh \"$(cat <<EOF\n";
            strcat(mymsg, "ERROR\nEOF\n)\" \"$(cat <<EOF\n");
            if (err == TOX_ERR_CONFERENCE_SEND_MESSAGE_CONFERENCE_NOT_FOUND)
                strcat(mymsg, "The conference number passed did not designate a valid conference");
            if (err == TOX_ERR_CONFERENCE_SEND_MESSAGE_TOO_LONG)
                strcat(mymsg, "The message is too long");
            if (err == TOX_ERR_CONFERENCE_SEND_MESSAGE_NO_CONNECTION)
                strcat(mymsg, "The client is not connected to the conference");
            if (err == TOX_ERR_CONFERENCE_SEND_MESSAGE_FAIL_SEND)
                strcat(mymsg, "The message packet failed to send");
            strcat(mymsg, "\n--msg--\n");
            strcat(mymsg, gmsg);
            strcat(mymsg, "\nEOF\n)\"");
//            system(mymsg);
            log_timestamp(mymsg);
        }
    }
}
static void get_msg_from_mt(Tox *m)
{
    if (PUBLIC_GROUP_NUM == 0)
    {
        if (joined_group == false)
        {
            PUBLIC_GROUP_NUM = Tox_Bot.last_connected;
            return;
        }
    }
    if (PUBLIC_GROUP_NUM == Tox_Bot.last_connected)
        return;

    if (gm_lock == true)
    {
        log_timestamp("gm task is busy");
        return;
    } else
        gm_lock = true;
    /** fd = popen(GM_SH_PATH, "r"); */
    fd_gm = popen("/run/user/1000/bot/gm.sh", "r");
    if (fd_gm == NULL)
    {
        log_timestamp("不能执行gm.sh");
        return;
    }
    gmsg[0] = '\0';
    while (1)
    {
        gmsgtmp[0] = '\0';
        if (fgets(gmsgtmp, TOX_MAX_MESSAGE_LENGTH, fd_gm) == NULL)
        {
            /** log_timestamp("gm ok"); */
            join_public_group(m);
            break;
        }
        log_timestamp("got msg from mt: %s", gmsgtmp);
        if (strlen(gmsgtmp) == 0)
            continue;
        if (strlen(gmsg)+strlen(gmsgtmp) > TOX_MAX_MESSAGE_LENGTH)
        {
            send_to_tox(m, gmsg, strlen(gmsg));
            gmsg[0] = '\0';
        }
        strcat(gmsg, gmsgtmp);

        /** if (strlen(gmsgtmp) >= TOX_MAX_MESSAGE_LENGTH-1) */
        /** { */
        /**     send_to_tox(m, gmsg, strlen(gmsg)); */
        /**     gmsg[0] = '\0'; */
        /**     send_to_tox(m, gmsgtmp, strlen(gmsgtmp)); */
        /** } else { */
        /**     if (strlen(gmsg)+strlen(gmsgtmp) > TOX_MAX_MESSAGE_LENGTH-1) */
        /**     { */
        /**         send_to_tox(m, gmsg, strlen(gmsg)); */
        /**         gmsg[0] = '\0'; */
        /**     } */
        /**     strcat(gmsg, gmsgtmp); */
        /** } */
    }
    //memset(msgfw, 0, sizeof(msgfw));
    send_to_tox(m, gmsg, strlen(gmsg));
    pclose(fd_gm);
    gm_lock = false;

}
// add by liqsliu

/* END CALLBACKS */

int save_data(Tox *m, const char *path)
{
    if (path == NULL) {
        goto on_error;
    }

    FILE *fp = fopen(path, "wb");

    if (fp == NULL) {
        return -1;
    }

    size_t data_len = tox_get_savedata_size(m);
    char *data = malloc(data_len);

    if (data == NULL) {
        goto on_error;
    }

    tox_get_savedata(m, (uint8_t *) data);

    if (fwrite(data, data_len, 1, fp) != 1) {
        free(data);
        fclose(fp);
        goto on_error;
    }

    free(data);
    fclose(fp);
    return 0;

on_error:
    log_error_timestamp(-1, "Warning: save_data failed");
    return -1;
}

static Tox *load_tox(struct Tox_Options *options, char *path)
{
    FILE *fp = fopen(path, "rb");
    Tox *m = NULL;

    if (fp == NULL) {
        TOX_ERR_NEW err;
        m = tox_new(options, &err);

        if (err != TOX_ERR_NEW_OK) {
            fprintf(stderr, "tox_new failed with error %d\n", err);
            return NULL;
        }

        save_data(m, path);
        return m;
    }

    off_t data_len = file_size(path);

    if (data_len == 0) {
        fprintf(stderr, "tox_new failed: toxbot save file is empty\n");
        fclose(fp);
        return NULL;
    }

    char data[data_len];

    if (fread(data, sizeof(data), 1, fp) != 1) {
        fclose(fp);
        return NULL;
    }

    TOX_ERR_NEW err;
    options->savedata_type = TOX_SAVEDATA_TYPE_TOX_SAVE;
    options->savedata_data = (uint8_t *) data;
    options->savedata_length = data_len;

    m = tox_new(options, &err);

    if (err != TOX_ERR_NEW_OK) {
        fprintf(stderr, "tox_new failed with error %d\n", err);
        return NULL;
    }

    fclose(fp);
    return m;
}

static void load_conferences(Tox *m)
{
    size_t num_chats = tox_conference_get_chatlist_size(m);

    if (num_chats == 0) {
        return;
    }

    uint32_t *chatlist = malloc(num_chats * sizeof(uint32_t));

    if (chatlist == NULL) {
        fprintf(stderr, "malloc() failed in load_conferences()\n");
        return;
    }

    tox_conference_get_chatlist(m, chatlist);

    for (size_t i = 0; i < num_chats; ++i) {
        uint32_t groupnumber = chatlist[i];

        Tox_Err_Conference_Get_Type type_err;
        Tox_Conference_Type type = tox_conference_get_type(m, groupnumber, &type_err);

        if (type_err != TOX_ERR_CONFERENCE_GET_TYPE_OK) {
            tox_conference_delete(m, groupnumber, NULL);
            continue;
        }

        if (group_add(groupnumber, type, NULL) != 0) {
            fprintf(stderr, "Failed to autoload group %d\n", groupnumber);
            tox_conference_delete(m, groupnumber, NULL);
            continue;
        }
    }

    free(chatlist);
}

static void print_usage(void)
{
    printf("usage: toxbot [OPTION] ...\n");
    printf("    -4, --ipv4              Force IPv4\n");
    printf("    -h, --help              Show this message and exit\n");
    printf("    -L, --no-lan            Disable LAN\n");
    printf("    -P, --HTTP-proxy        Use HTTP proxy. Requires: [IP] [port]\n");
    printf("    -p, --SOCKS5-proxy      Use SOCKS proxy. Requires: [IP] [port]\n");
    printf("    -t, --force-tcp         Force connections through TCP relays (DHT disabled)\n");
}

static void set_default_options(void)
{
    Options = (struct Options) {
        0
    };

    /* set any non-zero defaults here*/
    Options.proxy_type = TOX_PROXY_TYPE_NONE;
}

static void parse_args(int argc, char *argv[])
{
    set_default_options();

    static struct option long_opts[] = {
        {"ipv4", no_argument, 0, '4'},
        {"help", no_argument, 0, 'h'},
        {"no-lan", no_argument, 0, 'L'},
        {"SOCKS5-proxy", required_argument, 0, 'p'},
        {"HTTP-proxy", required_argument, 0, 'P'},
        {"force-tcp", no_argument, 0, 't'},
        {NULL, no_argument, NULL, 0},
    };

    const char *options_string = "4hLtp:P:";
    int opt = 0;
    int indexptr = 0;

    while ((opt = getopt_long(argc, argv, options_string, long_opts, &indexptr)) != -1) {
        switch (opt) {
            case '4': {
                Options.force_ipv4 = true;
                printf("Option set: Forcing IPV4\n");
                break;
            }

            case 'L': {
                Options.disable_lan = true;
                printf("Option set: LAN disabled\n");
                break;
            }

            case 'p': {
                Options.proxy_type = TOX_PROXY_TYPE_SOCKS5;
            }

            // Intentional fallthrough

            case 'P': {
                if (optarg == NULL) {
                    fprintf(stderr, "Invalid argument for option: %d", opt);
                    Options.proxy_type = TOX_PROXY_TYPE_NONE;
                    break;
                }

                if (Options.proxy_type != TOX_PROXY_TYPE_SOCKS5) {
                    Options.proxy_type = TOX_PROXY_TYPE_HTTP;
                }

                snprintf(Options.proxy_host, sizeof(Options.proxy_host), "%s", optarg);

                if (++optind > argc || argv[optind - 1][0] == '-') {
                    fprintf(stderr, "Error setting proxy\n");
                    exit(EXIT_FAILURE);
                }

                long int port = strtol(argv[optind - 1], NULL, 10);

                if (port <= 0 || port > MAX_PORT_RANGE) {
                    fprintf(stderr, "Invalid port given for proxy\n");
                    exit(EXIT_FAILURE);
                }

                Options.proxy_port = port;

                const char *proxy_str = Options.proxy_type == TOX_PROXY_TYPE_SOCKS5 ? "SOCKS5" : "HTTP";

                printf("Option set: %s proxy %s:%ld\n", proxy_str, optarg, port);
            }

            // Intentional fallthrough
            // we always want UDP disabled if proxy is set
            // don't change order, as -t must come after -P or -p

            case 't': {
                Options.disable_udp = true;
                printf("Option set: UDP/DHT disabled\n");
                break;
            }

            case 'h':

            // Intentional fallthrough

            default: {
                print_usage();
                exit(EXIT_SUCCESS);
            }
        }
    }
}

static void init_tox_options(struct Tox_Options *tox_opts)
{
    tox_options_default(tox_opts);

    tox_options_set_ipv6_enabled(tox_opts, !Options.force_ipv4);
    tox_options_set_udp_enabled(tox_opts, !Options.disable_udp);
    tox_options_set_proxy_type(tox_opts, Options.proxy_type);
    tox_options_set_local_discovery_enabled(tox_opts, !Options.disable_lan);

    if (Options.proxy_type != TOX_PROXY_TYPE_NONE) {
        tox_options_set_proxy_port(tox_opts, Options.proxy_port);
        tox_options_set_proxy_host(tox_opts, Options.proxy_host);
    }
}

static Tox *init_tox(void)
{
    Tox_Err_Options_New err;
    struct Tox_Options *tox_opts = tox_options_new(&err);

    if (!tox_opts || err != TOX_ERR_OPTIONS_NEW_OK) {
        fprintf(stderr, "Failed to initialize tox options: error %d\n", err);
        exit(EXIT_FAILURE);
    }

    init_tox_options(tox_opts);

    Tox *m = load_tox(tox_opts, DATA_FILE);

    tox_options_free(tox_opts);

    if (!m) {
        return NULL;
    }

    tox_callback_self_connection_status(m, cb_self_connection_change);
    tox_callback_friend_connection_status(m, cb_friend_connection_change);
    tox_callback_friend_request(m, cb_friend_request);
    tox_callback_friend_message(m, cb_friend_message);
    tox_callback_conference_invite(m, cb_group_invite);
    tox_callback_conference_title(m, cb_group_titlechange);

    // add by liqsliu
    tox_callback_conference_message(m, cb_conference_message);
    tox_callback_group_message(m, cb_group_message);
    tox_callback_group_invite(m, cb_group_invite2);
    // add by liqsliu


    size_t s_len = tox_self_get_status_message_size(m);

    if (s_len == 0) {
        const char *statusmsg = "Send me the the command 'help' for more info";
        tox_self_set_status_message(m, (uint8_t *) statusmsg, strlen(statusmsg), NULL);
    }

    size_t n_len = tox_self_get_name_size(m);

    if (n_len == 0) {
        tox_self_set_name(m, (uint8_t *) "Tox_Bot", strlen("Tox_Bot"), NULL);
    }

    return m;
}

/* TODO: hardcoding is bad stop being lazy */
static struct toxNodes {
    const char *ip;
    uint16_t    port;
    const char *key;
} nodes[] = {
    { "95.79.50.56", 33445, "8E7D0B859922EF569298B4D261A8CCB5FEA14FB91ED412A7603A585A25698832" },
    { "85.143.221.42", 33445, "DA4E4ED4B697F2E9B000EEFE3A34B554ACD3F45F5C96EAEA2516DD7FF9AF7B43" },
    { "46.229.52.198", 33445, "813C8F4187833EF0655B10F7752141A352248462A567529A38B6BBF73E979307" },
    { "144.217.167.73", 33445, "7E5668E0EE09E19F320AD47902419331FFEE147BB3606769CFBE921A2A2FD34C" },
    { "198.199.98.108", 33445, "BEF0CFB37AF874BD17B9A8F9FE64C75521DB95A37D33C5BDB00E9CF58659C04F" },
    { "81.169.136.229", 33445, "E0DB78116AC6500398DDBA2AEEF3220BB116384CAB714C5D1FCD61EA2B69D75E" },
    { "205.185.115.131", 53, "3091C6BEB2A993F1C6300C16549FABA67098FF3D62C6D253828B531470B53D68" },
    { "46.101.197.175", 33445, "CD133B521159541FB1D326DE9850F5E56A6C724B5B8E5EB5CD8D950408E95707" },
    { "195.201.7.101", 33445, "B84E865125B4EC4C368CD047C72BCE447644A2DC31EF75BD2CDA345BFD310107" },
    { "168.138.203.178", 33445, "6D04D8248E553F6F0BFDDB66FBFB03977E3EE54C432D416BC2444986EF02CC17" },
    { "5.19.249.240", 38296, "DA98A4C0CD7473A133E115FEA2EBDAEEA2EF4F79FD69325FC070DA4DE4BA3238" },
    { "209.59.144.175", 33445, "214B7FEA63227CAEC5BCBA87F7ABEEDB1A2FF6D18377DD86BF551B8E094D5F1E" },
    { "188.225.9.167", 33445, "1911341A83E02503AB1FD6561BD64AF3A9D6C3F12B5FBB656976B2E678644A67" },
    { "122.116.39.151", 33445, "5716530A10D362867C8E87EE1CD5362A233BAFBBA4CF47FA73B7CAD368BD5E6E" },
    { "195.123.208.139", 33445, "534A589BA7427C631773D13083570F529238211893640C99D1507300F055FE73" },
    { "104.225.141.59", 43334, "933BA20B2E258B4C0D475B6DECE90C7E827FE83EFA9655414E7841251B19A72C" },
    { "137.74.42.224", 33445, "A95177FA018066CF044E811178D26B844CBF7E1E76F140095B3A1807E081A204" },
    { "172.105.109.31", 33445, "D46E97CF995DC1820B92B7D899E152A217D36ABE22730FEA4B6BF1BFC06C617C" },
    { "91.146.66.26", 33445, "B5E7DAC610DBDE55F359C7F8690B294C8E4FCEC4385DE9525DBFA5523EAD9D53" },
    { NULL, 0, NULL },
};

static void bootstrap_DHT(Tox *m)
{
    for (int i = 0; nodes[i].ip; ++i) {
        char *key = hex_string_to_bin(nodes[i].key);

        TOX_ERR_BOOTSTRAP err;
        tox_bootstrap(m, nodes[i].ip, nodes[i].port, (uint8_t *) key, &err);

        if (err != TOX_ERR_BOOTSTRAP_OK) {
            fprintf(stderr, "Failed to bootstrap DHT: %s %d (error %d)\n", nodes[i].ip, nodes[i].port, err);
        }

        tox_add_tcp_relay(m, nodes[i].ip, nodes[i].port, (uint8_t *) key, &err);

        if (err != TOX_ERR_BOOTSTRAP_OK) {
            fprintf(stderr, "Failed to add TCP relay: %s %d (error %d)\n", nodes[i].ip, nodes[i].port, err);
        }

        free(key);
    }
}

static void print_profile_info(Tox *m)
{
    printf("Tox_Bot version %s\n", VERSION);
    printf("Toxcore version %d.%d.%d\n", tox_version_major(), tox_version_minor(), tox_version_patch());
    printf("Tox ID: ");

    char address[TOX_ADDRESS_SIZE];
    tox_self_get_address(m, (uint8_t *) address);

    for (int i = 0; i < TOX_ADDRESS_SIZE; ++i) {
        char d[3];
        snprintf(d, sizeof(d), "%02X", address[i] & 0xff);
        printf("%s", d);
    }

    printf("\n");

    char name[TOX_MAX_NAME_LENGTH];
    size_t len = tox_self_get_name_size(m);
    tox_self_get_name(m, (uint8_t *) name);
    name[len] = '\0';

    size_t numfriends = tox_self_get_friend_list_size(m);
    size_t num_chats = tox_conference_get_chatlist_size(m);

    printf("Name: %s\n", name);
    printf("Contacts: %lu\n", numfriends);
    printf("Active groups: %lu\n", num_chats);
}

static void purge_inactive_friends(Tox *m)
{
    size_t numfriends = tox_self_get_friend_list_size(m);

    if (numfriends == 0) {
        return;
    }

    uint32_t friend_list[numfriends];
    tox_self_get_friend_list(m, friend_list);

    for (size_t i = 0; i < numfriends; ++i) {
        uint32_t friendnum = friend_list[i];

        if (!tox_friend_exists(m, friendnum)) {
            continue;
        }

        TOX_ERR_FRIEND_GET_LAST_ONLINE err;
        uint64_t last_online = tox_friend_get_last_online(m, friendnum, &err);

        if (err != TOX_ERR_FRIEND_GET_LAST_ONLINE_OK) {
            continue;
        }

        if (get_time() - last_online > Tox_Bot.inactive_limit) {
            tox_friend_delete(m, friendnum, NULL);
        }
    }
}

static void purge_empty_groups(Tox *m)
{
    for (uint32_t i = 0; i < Tox_Bot.chats_idx; ++i) {
        if (!Tox_Bot.g_chats[i].active) {
            continue;
        }

        TOX_ERR_CONFERENCE_PEER_QUERY err;
        uint32_t num_peers = tox_conference_peer_count(m, Tox_Bot.g_chats[i].groupnum, &err);

        if (err != TOX_ERR_CONFERENCE_PEER_QUERY_OK || num_peers <= 1) {
            log_timestamp("Deleting empty group %d", Tox_Bot.g_chats[i].groupnum);
            tox_conference_delete(m, Tox_Bot.g_chats[i].groupnum, NULL);
            group_leave(i);

            if (i >= Tox_Bot.chats_idx) {   // group_leave modifies chats_idx
                return;
            }
        }
    }
}

/* Return true if we should attempt to purge empty groups.
 *
 * Empty groups are purged on an interval, but only if we have a stable connection
 * to the Tox network.
 */
static bool check_group_purge(time_t last_group_purge, time_t cur_time, TOX_CONNECTION connection_status)
{
    if (!timed_out(last_group_purge, cur_time, GROUP_PURGE_INTERVAL)) {
        return false;
    }

    if (connection_status == TOX_CONNECTION_NONE) {
        return false;
    }

    if (!timed_out(Tox_Bot.last_connected, cur_time, GROUP_PURGE_CONNECT_TIMEOUT)) {
        return false;
    }

    return true;
}

/* Attempts to rename legacy toxbot save file to new name
 *
 * Return 0 on successful rename, or if legacy file does not exist.
 * Return -1 if both legacy file and new file exist. If this occurrs the user needs to manually sort
 *   the situation out.
 * Return -2 if file rename operation is unsuccessful.
 */
static int legacy_data_file_rename(void)
{
    if (!file_exists(DATA_FILE_PRE_0_1_1)) {
        return 0;
    }

    if (file_exists(DATA_FILE)) {
        return -1;
    }

    if (rename(DATA_FILE_PRE_0_1_1, DATA_FILE) != 0) {
        return -2;
    }

    printf("Renaming legacy toxbot save file to '%s'\n", DATA_FILE);

    return 0;
}

int main(int argc, char **argv)
{
    signal(SIGINT, catch_SIGINT);
    umask(S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

    int ret = legacy_data_file_rename() ;

    if (ret != 0) {
        fprintf(stderr, "Failed to rename legacy data file. Error: %d\n", ret);
        exit(EXIT_FAILURE);
    }

    parse_args(argc, argv);

    Tox *m = init_tox();

    if (m == NULL) {
        exit(EXIT_FAILURE);
    }

    init_toxbot_state();
    load_conferences(m);
    print_profile_info(m);

    time_t cur_time = get_time();

    uint64_t last_friend_purge = cur_time;
    uint64_t last_group_purge = cur_time;

    while (!FLAG_EXIT) {
        TOX_CONNECTION connection_status = tox_self_get_connection_status(m);

        if (connection_status == TOX_CONNECTION_NONE
                && timed_out(Tox_Bot.last_bootstrap, cur_time, BOOTSTRAP_INTERVAL)) {
            log_timestamp("Bootstrapping to network...");
            bootstrap_DHT(m);
            Tox_Bot.last_bootstrap = cur_time;
        }

        if (connection_status != TOX_CONNECTION_NONE && timed_out(last_friend_purge, cur_time, FRIEND_PURGE_INTERVAL)) {
            purge_inactive_friends(m);
            save_data(m, DATA_FILE);
            last_friend_purge = cur_time;
        }

        if (check_group_purge(last_group_purge, cur_time, connection_status)) {
            purge_empty_groups(m);
            last_group_purge = cur_time;
        }

        tox_iterate(m, NULL);


// add by liqsliu
get_msg_from_mt(m);
// add by liqsliu

        usleep(tox_iteration_interval(m) * 1000);

        cur_time = get_time();
    }

    exit_toxbot(m);

    return 0;
}

