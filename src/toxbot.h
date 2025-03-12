/*  toxbot.h
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

#ifndef TOXBOT_H
#define TOXBOT_H

#include <stdint.h>
#include <tox/tox.h>
#include "groupchats.h"

#define MAX_NUM_GROUPS 256

#define DATA_FILE        "toxbot.tox"
#define MASTERLIST_FILE  "masterkeys"
#define BLOCKLIST_FILE   "blockedkeys"

struct Tox_Bot {
    time_t     start_time;  // time toxbot was started
    time_t     last_connected;  // time we last connected to the network
    time_t     last_bootstrap;  // last time we tried to bootstrap
    uint64_t   inactive_limit;  // how often we purge inactive contacts
    int        default_groupnum;  // the group that invite commands with no ID default to
    int        num_online_friends;
    int        chats_idx;

    struct Group_Chat *g_chats;
};

int load_Masters(const char *path);
int save_data(Tox *m, const char *path);
bool friend_is_master(Tox *m, uint32_t friendnumber);

// add by liqsliu
#define BOT_NAME "bot"
#define GROUP_NAME "wtfipfs"
#define CHAT_ID "5CD71E298857CA3B502BE58383E3AF7122FCDE5BF46D5424192234DF83A76A66"
#define CHAT_ID2 "154b3973bd0e66304fd6179a8a54759073649e09e6e368f0334fc6ed666ab762"

#define SH_PATH "/run/user/1000/bot"
#define SM_SH_PATH "bash /run/user/1000/bot/sm.sh \"$(cat <<EOF\n"
#define GM_SH_PATH "bash /run/user/1000/bot/gm_stream.sh"

int rejoin_public_group(Tox *m, Tox_Group_Number gn);
int join_public_group(Tox *m);
int join_public_group_by_chat_id(Tox *m, char *chat_id);
// add by liqsliu

#endif /* TOXBOT_H */

