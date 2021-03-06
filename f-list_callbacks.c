/*
 * F-List Pidgin - a libpurple protocol plugin for F-Chat
 *
 * Copyright 2011 F-List Pidgin developers.
 *
 * This file is part of F-List Pidgin.
 *
 * F-List Pidgin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * F-List Pidgin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with F-List Pidgin.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "f-list_callbacks.h"

static GHashTable *callbacks = NULL;

//NOT IMPLEMENTED: AWC // ADMIN ALTERNATE WATCH

//WILL NOT IMPLEMENT: OPP // ???????
//TODO: RAN // ADVERTISE PRIVATE CHANNEL


static GHashTable *flist_global_ops_new() {
    return g_hash_table_new_full((GHashFunc)flist_str_hash, (GEqualFunc)flist_str_equal, g_free, NULL);
}
static void flist_purple_find_chats_in_node(PurpleAccount *pa, PurpleBlistNode *n, GSList **current) {
    while(n) {
        if(n->type == PURPLE_BLIST_CHAT_NODE && PURPLE_CHAT(n)->account == pa) {
            *current = g_slist_prepend(*current, n);
        }
        if(n->child) flist_purple_find_chats_in_node(pa, n->child, current);
        n = n->next;
    }
}
/* this might not be the best way to find buddies ... */
/*static void flist_purple_find_buddies_in_node(PurpleAccount *pa, PurpleBlistNode *n, GSList **current) {
    while(n) {
        if(PURPLE_BLIST_NODE_IS_BUDDY(n) && PURPLE_BUDDY(n)->account == pa) {
            purple_debug_info(FLIST_DEBUG, "found buddy: %s %x\n", purple_buddy_get_name(PURPLE_BUDDY(n)), n);
            *current = g_slist_prepend(*current, n);
        }
        if(n->child) flist_purple_find_buddies_in_node(pa, n->child, current);
        n = n->next;
    }
}*/

static void flist_got_online(FListAccount *fla) {
    PurpleAccount *pa = fla->pa;
    PurpleGroup *chat_group;
    GSList *chats = NULL;
    GSList *cur;

    chat_group = flist_get_chat_group(fla);

    if(chat_group) {
        flist_purple_find_chats_in_node(pa, PURPLE_BLIST_NODE(chat_group)->child, &chats);
        cur = chats;
        while(cur) {
            purple_blist_remove_chat(cur->data);
            cur = g_slist_next(cur);
        }
        g_slist_free(chats);
    }

    purple_connection_set_state(fla->pc, PURPLE_CONNECTED);
    fla->online = TRUE;

    /* If we're syncing status, set protocol internal status from purple one */
    if (fla->sync_status) {
        PurpleStatus *ps = purple_account_get_active_status(pa);
        flist_set_internal_status_from_purple_status(fla, ps);
    }

    flist_update_server_status(fla);

    /* Start managing the friends list. */
    flist_friends_login(fla);
}

static gboolean flist_process_HLO(FListAccount *fla, JsonObject *root) {
    const gchar *message;
    message = json_object_get_string_member(root, "message");

    purple_debug_info(FLIST_DEBUG, "Got server hello: %s\n", message);
    return TRUE;
}

static gboolean flist_process_KID(FListAccount *fla, JsonObject *root) {
    const gchar *type;
    gchar *decoded_value, *decoded_key;
    gchar *text;
    PurpleConversation *convo = flist_recall_conversation(fla);

    type = json_object_get_string_member(root, "type");
    if (g_strcmp0(type, "start") == 0) {
        text = g_strdup_printf("%s", json_object_get_string_member(root, "message"));
        purple_conversation_write(convo, NULL, text, PURPLE_MESSAGE_SYSTEM, time(NULL));
        g_free(text);
    } else if (g_strcmp0(type, "custom") == 0) {
        decoded_value = flist_html_unescape_utf8(json_object_get_string_member(root, "value"));
        decoded_key = flist_html_unescape_utf8(json_object_get_string_member(root, "key"));
        text = g_strdup_printf("<i>%s</i>: %s",
                decoded_key,
                decoded_value
                );
        g_free(decoded_key);
        g_free(decoded_value);

        purple_conversation_write(convo, NULL, text, PURPLE_MESSAGE_SYSTEM, time(NULL));
        g_free(text);
    } else if (g_strcmp0(type, "end") == 0) {
        text = g_strdup_printf("%s", json_object_get_string_member(root, "message"));
        purple_conversation_write(convo, NULL, text, PURPLE_MESSAGE_SYSTEM, time(NULL));
        g_free(text);
    }
    return TRUE;
}

static gboolean flist_process_UPT(FListAccount *fla, JsonObject *root) {
    const gchar *startstring;
    gchar *msgbuf;
    gint64 accepted, channels, users, maxusers;
    GString *message;

    startstring = json_object_get_string_member(root, "startstring");
    accepted = json_object_get_int_member(root, "accepted");
    channels = json_object_get_int_member(root, "channels");
    users = json_object_get_int_member(root, "users");
    maxusers = json_object_get_int_member(root, "maxusers");


    message = g_string_new("F-Chat server statistics\n");
    if (startstring) g_string_append_printf(message, "Server start date: %s\n",startstring);
    if (accepted) g_string_append_printf(message, "Accepted users: %" G_GINT64_FORMAT "\n",accepted);
    if (channels) g_string_append_printf(message, "Current channels: %" G_GINT64_FORMAT "\n",channels);
    if (users) g_string_append_printf(message, "Current users: %" G_GINT64_FORMAT "\n",users);
    if (maxusers) g_string_append_printf(message, "Maximum users: %" G_GINT64_FORMAT "\n",maxusers);
    msgbuf = g_string_free(message, FALSE);

    PurpleConversation *convo = flist_recall_conversation(fla);
    purple_conversation_write(convo, NULL, msgbuf, PURPLE_MESSAGE_SYSTEM, time(NULL));
    g_free(msgbuf);

    return TRUE;
}

static gboolean flist_process_VAR(FListAccount *fla, JsonObject *root) {
    const gchar *variable_name;
    JsonArray *values;
    guint len,i;

    variable_name = json_object_get_string_member(root, "variable");

    //  chat_max: Maximum number of bytes allowed with MSG.
    if (g_strcmp0(variable_name, "chat_max") == 0) {
        fla->chat_max = (gsize)json_object_get_int_member(root, "value");
        purple_debug_info(FLIST_DEBUG, "Received channel message size limit: %"G_GSIZE_FORMAT"\n", fla->chat_max);
        return TRUE;
    }

    //  priv_max: Maximum number of bytes allowed with PRI.
    if (g_strcmp0(variable_name, "priv_max") == 0) {
        fla->priv_max = (gsize)json_object_get_int_member(root, "value");
        purple_debug_info(FLIST_DEBUG, "Received private message size limit: %"G_GSIZE_FORMAT"\n", fla->priv_max);
        return TRUE;
    }

    //  lfrp_max: Maximum number of bytes allowed with LRP.
    if (g_strcmp0(variable_name, "lfrp_max") == 0) {
        fla->lfrp_max = (gsize)json_object_get_int_member(root, "value");
        purple_debug_info(FLIST_DEBUG, "Received ad message size limit: %"G_GSIZE_FORMAT"\n", fla->lfrp_max);
        return TRUE;
    }

    //  lfrp_flood: Required seconds between LRP messages.
    if (g_strcmp0(variable_name, "lfrp_flood") == 0) {
        fla->lfrp_flood = (gfloat)json_object_get_double_member(root, "value");
        purple_debug_info(FLIST_DEBUG, "Received ad message flood threshold: %f seconds\n", fla->lfrp_flood);
        return TRUE;
    }

    //  msg_flood: Required seconds between MSG messages.
    if (g_strcmp0(variable_name, "msg_flood") == 0) {
        fla->msg_flood = (gfloat)json_object_get_double_member(root, "value");
        purple_debug_info(FLIST_DEBUG, "Received channel message flood threshold: %f seconds\n", fla->msg_flood);
        return TRUE;
    }

    //  permissions: Permissions mask for this character.
    if (g_strcmp0(variable_name, "permissions") == 0) {
        fla->permissions = (guint32)json_object_get_int_member(root, "value");
        purple_debug_info(FLIST_DEBUG, "Received permission mask for character %s: %"G_GUINT32_FORMAT"\n", fla->character, fla->permissions);
        return TRUE;
    }

    //  icon_blacklist: An array of channels that do not allow (e)icons.
    if (g_strcmp0(variable_name,"icon_blacklist") == 0) {
        values = json_object_get_array_member(root, "value");

        if (fla->icon_blacklist) {
            flist_g_slist_free_full(fla->icon_blacklist, g_free);
            fla->icon_blacklist = NULL;
        }

        len = json_array_get_length(values);
        for(i = 0; i < len; i++) {
            fla->icon_blacklist = g_slist_append(fla->icon_blacklist,
                    g_strdup(json_array_get_string_element(values, i)));
        }
        purple_debug_info(FLIST_DEBUG, "Received icon blacklist for server: %u channels\n", g_slist_length(fla->icon_blacklist));
        return TRUE;
    }
    // Unhandled
    return FALSE;
}

static gboolean flist_process_ERR(FListAccount *fla, JsonObject *root) {
    const gchar *message;
    long number;

    message = json_object_get_string_member(root, "message");

    if(fla->connection_status == FLIST_IDENTIFY) {
        purple_connection_error_reason(fla->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, message);
        return FALSE;
    }

    number = json_object_get_int_member(root, "number");
    switch(number) {
    case FLIST_ERROR_PROFILE_FLOOD: //too many profile requests
        flist_profile_process_flood(fla, message);
        return TRUE;
    case FLIST_ERROR_ALREADY_LOGGED_IN: //The user logged in somewhere else
        purple_connection_error_reason(fla->pc, PURPLE_CONNECTION_ERROR_NAME_IN_USE, message);
        return TRUE;
    }

    purple_notify_warning(fla->pc, "F-List Error", "An error has occurred on F-List.", message);

    return TRUE;
    //TODO: error messages have context!
}

static gboolean flist_process_NLN(FListAccount *fla, JsonObject *root) {
    PurpleAccount *pa = fla->pa;

    g_return_val_if_fail(root, TRUE);

    FListCharacter *character = g_new0(FListCharacter, 1);
    character->name = g_strdup(json_object_get_string_member(root, "identity"));
    character->gender = flist_parse_gender(json_object_get_string_member(root, "gender"));
    character->status = flist_parse_status(json_object_get_string_member(root, "status"));
    character->status_message = g_strdup("");

    g_hash_table_replace(fla->all_characters, g_strdup(flist_normalize(pa, character->name)), character);
    fla->character_count += 1;

    flist_update_friend(fla, character->name, TRUE, FALSE);

    if(!fla->online && flist_str_equal(fla->character, character->name)) {
        flist_got_online(fla);
    }

    return TRUE;
}

gint flist_channel_cmp(FListRoomlistChannel *c1, FListRoomlistChannel *c2) {
    return ((gint) c2->users) - ((gint) c1->users);
}

static void handle_roomlist(PurpleRoomlist *roomlist, JsonArray *json, gboolean public) {
    int i, len;
    GSList *channels = NULL, *cur;

    len = json_array_get_length(json);
    for(i = 0; i < len; i++) {
        JsonObject *object = json_array_get_object_element(json, i);
        FListRoomlistChannel *c = g_new0(FListRoomlistChannel, 1);
        const gchar *title, *name;
        int characters;
        title = json_object_get_string_member(object, "title");
        name = json_object_get_string_member(object, "name");
        characters = json_object_get_parse_int_member(object, "characters", NULL);
        c->name = name ? g_strdup(name) : NULL;
        c->title = title ? flist_html_unescape_utf8(title) : NULL;
        c->users = characters;
        c->is_public = public;
        channels = g_slist_prepend(channels, c);
    }

    channels = g_slist_sort(channels, (GCompareFunc) flist_channel_cmp);

    cur = channels;
    while(cur) {
        FListRoomlistChannel *c = cur->data;
        PurpleRoomlistRoom *room = purple_roomlist_room_new(PURPLE_ROOMLIST_ROOMTYPE_ROOM, c->title ? c->title : c->name, NULL);
        purple_roomlist_room_add_field(roomlist, room, c->name);
        purple_roomlist_room_add_field(roomlist, room, c->is_public ? "Public" : "Private");
        purple_roomlist_room_add_field(roomlist, room, GINT_TO_POINTER(c->users));
        purple_roomlist_room_add(roomlist, room);

        if(c->name) g_free(c->name);
        if(c->title) g_free(c->title);
        g_free(c);

        cur = g_slist_next(cur);
    }

    g_slist_free(channels);
}

static gboolean flist_process_CHA(FListAccount *fla, JsonObject *root) {
    JsonArray *channels;

    if(fla->roomlist == NULL) return TRUE;

    g_return_val_if_fail(root, TRUE);
    channels = json_object_get_array_member(root, "channels");
    g_return_val_if_fail(channels, TRUE);

    handle_roomlist(fla->roomlist, channels, TRUE);

    purple_roomlist_set_in_progress(fla->roomlist, TRUE);
    flist_request(fla, FLIST_REQUEST_PRIVATE_CHANNEL_LIST, NULL);

    return TRUE;
}

static gboolean flist_process_ORS(FListAccount *fla, JsonObject *root) {
    JsonArray *channels;

    if(fla->roomlist == NULL) return TRUE;

    g_return_val_if_fail(root, TRUE);
    channels = json_object_get_array_member(root, "channels");
    g_return_val_if_fail(channels, TRUE);

    handle_roomlist(fla->roomlist, channels, FALSE);

    purple_roomlist_set_in_progress(fla->roomlist, FALSE);
    return TRUE;
}

static gboolean flist_process_STA(FListAccount *fla, JsonObject *root) {
    FListCharacter *character;
    const gchar *name, *status, *status_message;

    g_return_val_if_fail(root, TRUE);
    name = json_object_get_string_member(root, "character");
    status = json_object_get_string_member(root, "status");
    status_message = json_object_get_string_member(root, "statusmsg");
    g_return_val_if_fail(name, TRUE);

    character = g_hash_table_lookup(fla->all_characters, name);
    if(character) {
        if(status) {
            character->status = flist_parse_status(status);

            // Update user ranks, in case status changed to Looking
            // and we need to update the icon in a channel
            flist_update_user_chats_rank(fla, name);
        }
        if(status_message) {
            if (character->status_message)
                g_free(character->status_message);

            // The server already escapes HTML entities for us, no need to escape them again
            character->status_message = g_strdup(status_message);
        }
        flist_update_friend(fla, name, FALSE, FALSE);
    }

    return TRUE;
}

static gboolean flist_process_FLN(FListAccount *fla, JsonObject *root) {
    const gchar *character;

    g_return_val_if_fail(root, TRUE);

    character = json_object_get_string_member(root, "character");
    g_hash_table_remove(fla->all_characters, character);
    fla->character_count -= 1;

    flist_update_friend(fla, character, FALSE, FALSE);
    flist_update_user_chats_offline(fla, character);

    return TRUE;
}

static gboolean flist_process_RLL(FListAccount *fla, JsonObject *root) {
    PurpleAccount *pa = fla->pa;
    PurpleMessageFlags flags = PURPLE_MESSAGE_SEND;
    PurpleConversation *convo;
    const gchar *character;
    const gchar *message;
    const gchar *target;
    gchar *parsed;

    character = json_object_get_string_member(root, "character");
    message = json_object_get_string_member(root, "message");

    if (flist_ignore_character_is_ignored(fla, character))
        return TRUE;

    // Roll happened in a channel
    if (json_object_has_member(root, "channel"))
    {
        target = json_object_get_string_member(root, "channel");
        convo = purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT, target, pa);
        if(!convo) {
            purple_debug_error(FLIST_DEBUG, "Received message for channel %s, but we are not in this channel.\n", target);
            return TRUE;
        }

        if (!flist_get_channel_show_chat(fla, target))
            return TRUE;

        if (purple_utf8_strcasecmp(character, fla->character)){
            flags = PURPLE_MESSAGE_RECV;
        }

        parsed = flist_bbcode_to_html(fla, convo, message);
        serv_got_chat_in(fla->pc, purple_conv_chat_get_id(PURPLE_CONV_CHAT(convo)), character, flags, parsed, time(NULL));

    }

    // Roll happened in a private message
    else
    {
        target = json_object_get_string_member(root, "recipient");

        // If we were the one who sent the roll we'll swap target and character variables
        // so we can use the same code below for both cases
        if (!purple_utf8_strcasecmp(target, fla->character)) {
            target = character;
            flags = PURPLE_MESSAGE_RECV;
        }

        // Get or create conversation
        convo = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, target, pa);
        if(!convo)
            convo = purple_conversation_new(PURPLE_CONV_TYPE_IM, pa, target);

        parsed = flist_bbcode_to_html(fla, convo, message);
        purple_conv_im_write(PURPLE_CONV_IM(convo), character, parsed, flags, time(NULL));
    }

    purple_debug_info(FLIST_DEBUG, "Roll: %s (Target: %s, Character: %s, Message: %s)\n", parsed, target, character, message);

    g_free(parsed);
    return TRUE;
}

static gboolean flist_process_MSG(FListAccount *fla, JsonObject *root) {
    PurpleAccount *pa = fla->pa;
    PurpleConversation *convo;
    const gchar *character;
    const gchar *message;
    const gchar *channel;
    gchar *parsed;
    gboolean show;
    PurpleMessageFlags flags;

    channel = json_object_get_string_member(root, "channel");
    character = json_object_get_string_member(root, "character");
    message = json_object_get_string_member(root, "message");

    convo = purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT, channel, pa);
    if(!convo) {
        purple_debug_error(FLIST_DEBUG, "Received message for channel %s, but we are not in this channel.\n", channel);
        return TRUE;
    }

    show = flist_get_channel_show_chat(fla, channel);
    flags = (show ? PURPLE_MESSAGE_RECV : PURPLE_MESSAGE_INVISIBLE);

    if (g_ascii_strncasecmp(message, "/warn", 5) == 0)
    {
        FListChannel *fc = flist_channel_find(fla, channel);

        if (fc && (g_list_find_custom(fc->operators, character, (GCompareFunc) purple_utf8_strcasecmp) || g_hash_table_lookup(fla->global_ops, character)))
        {
            flist_channel_print_op_warning(convo, character, &message[6]);
            return TRUE;
        }
    }

    parsed = flist_bbcode_to_html(fla, convo, message);

    purple_debug_info(FLIST_DEBUG, "Message: %s\n", parsed);
    if(show && !flist_ignore_character_is_ignored(fla, character)) {
        serv_got_chat_in(fla->pc, purple_conv_chat_get_id(PURPLE_CONV_CHAT(convo)), character, flags, parsed, time(NULL));
    }
    g_free(parsed);
    return TRUE;
}

//TODO: Record advertisements for later use.
static gboolean flist_process_LRP(FListAccount *fla, JsonObject *root) {
    PurpleAccount *pa = fla->pa;
    PurpleConversation *convo;
    const gchar *character;
    const gchar *message;
    const gchar *channel;
    gchar *full_message, *parsed;
    gboolean show;
    PurpleMessageFlags flags;

    channel = json_object_get_string_member(root, "channel");
    character = json_object_get_string_member(root, "character");
    message = json_object_get_string_member(root, "message");

    convo = purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT, channel, pa);
    if(!convo) {
        purple_debug_error(FLIST_DEBUG, "Received advertisement for channel %s, but we are not in this channel.\n", channel);
        return TRUE;
    }

    show = flist_get_channel_show_ads(fla, channel);
    flags = (show ? PURPLE_MESSAGE_RECV : PURPLE_MESSAGE_INVISIBLE);

    full_message = g_strdup_printf("<body bgcolor=\"%s\">[b](Roleplay Ad)[/b] %s</body>", purple_account_get_string(fla->pa, "ads_background", FLIST_RPAD_DEFAULT_BACKGROUND), message);
    parsed = flist_bbcode_to_html(fla, convo, full_message);
    purple_debug_info(FLIST_DEBUG, "Advertisement: %s\n", parsed);
    if(show && !flist_ignore_character_is_ignored(fla, character)) {
        serv_got_chat_in(fla->pc, purple_conv_chat_get_id(PURPLE_CONV_CHAT(convo)), character, flags, parsed, time(NULL));
    }
    g_free(parsed);
    g_free(full_message);
    return TRUE;
}

static gboolean flist_process_BRO(FListAccount *fla, JsonObject *root) {
    const gchar *message, *character;
    gchar *parsed;

    /* Ignore notification if the user doesn't want to see it */
    if (!fla->receive_notifications) {
        return TRUE;
    }

    message = json_object_get_string_member(root, "message");
    character = json_object_get_string_member(root, "character");

    g_return_val_if_fail(message, TRUE);
    if(!character) character = GLOBAL_NAME;

    parsed = flist_bbcode_to_html(fla, NULL, message);
    serv_got_im(fla->pc, GLOBAL_NAME, parsed, PURPLE_MESSAGE_RECV, time(NULL));

    g_free(parsed);
    return TRUE;
}

static gboolean flist_process_SYS(FListAccount *fla, JsonObject *root) {
    PurpleAccount *pa = fla->pa;
    const gchar *message;
    const gchar *channel, *sender;
    gchar *parsed, *final;
    PurpleConversation *convo;

    g_return_val_if_fail(root, TRUE);
    message = json_object_get_string_member(root, "message");
    channel = json_object_get_string_member(root, "channel");
    sender = json_object_get_string_member(root, "sender");

    if(channel) {
        convo = purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT, channel, pa);
        if(!convo) {
            purple_debug_error(FLIST_DEBUG, "Received system message for channel %s, but we are not in this channel.\n", channel);
            return TRUE;
        }
        parsed = flist_bbcode_to_html(fla, convo, message);
        purple_conv_chat_write(PURPLE_CONV_CHAT(convo), "System", parsed, PURPLE_MESSAGE_SYSTEM, time(NULL));
        g_free(parsed);
    } else if(sender) {
        parsed = flist_bbcode_to_html(fla, NULL, message);
        serv_got_im(fla->pc, sender, parsed, PURPLE_MESSAGE_SYSTEM, time(NULL));
        g_free(parsed);
    } else {
        parsed = flist_bbcode_to_html(fla, NULL, message);
        final = g_strdup_printf("(System) %s", parsed);
        serv_got_im(fla->pc, GLOBAL_NAME, final, PURPLE_MESSAGE_SYSTEM, time(NULL));
        g_free(final);
        g_free(parsed);
    }

    //TODO: parse out channel invitations!
    return TRUE;
}

static gboolean flist_process_CON(FListAccount *fla, JsonObject *root) {
    gboolean success;

    fla->characters_remaining = json_object_get_parse_int_member(root, "count", &success);

    if(!success) {
        purple_connection_error_reason(fla->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, "Could not parse the number of users online.");
        return FALSE;
    }

    return TRUE;
}

static gboolean flist_process_LIS(FListAccount *fla, JsonObject *root) {
    PurpleAccount *pa = fla->pa;
    JsonArray *characters;
    guint32 len;
    JsonArray *character_array;
    guint i;

    g_return_val_if_fail(root, TRUE);
    characters = json_object_get_array_member(root, "characters");
    g_return_val_if_fail(characters, TRUE);

    len = json_array_get_length(characters);
    for(i = 0; i < len; i++) {
        FListCharacter *character = g_new0(FListCharacter, 1);
        character_array = json_array_get_array_element(characters, i);

        g_return_val_if_fail(character_array, TRUE);
        g_return_val_if_fail(json_array_get_length(character_array) == 4, TRUE);

        character->name = g_strdup(json_array_get_string_element(character_array, 0));
        character->gender = flist_parse_gender(json_array_get_string_element(character_array, 1));
        character->status = flist_parse_status(json_array_get_string_element(character_array, 2));

        if (character->status_message)
            g_free(character->status_message);

        // The server already escapes HTML entities for us, no need to escape them again
        character->status_message = g_strdup(json_array_get_string_element(character_array, 3));

        g_hash_table_replace(fla->all_characters, g_strdup(flist_normalize(pa, character->name)), character);
        flist_update_friend(fla, character->name, TRUE, FALSE);
    }

    return TRUE;
}

static gboolean flist_process_AOP(FListAccount *fla, JsonObject *root) {
    PurpleAccount *pa = fla->pa;
    const gchar *character;
    gchar *identity;

    character = json_object_get_string_member(root, "character");
    g_return_val_if_fail(character, TRUE);
    if(!fla->global_ops) fla->global_ops = flist_global_ops_new();

    identity = g_strdup(character);
    g_hash_table_replace(fla->global_ops, identity, identity);
    flist_update_user_chats_rank(fla, identity);

    g_free(identity);

    purple_prpl_got_account_actions(pa);

    if (fla->receive_notifications) {
        gchar *message = g_strdup_printf("%s is now a global operator.", character);
        serv_got_im(fla->pc, GLOBAL_NAME, message, PURPLE_MESSAGE_RECV, time(NULL));
        g_free(message);
    }

    return TRUE;
}

static gboolean flist_process_DOP(FListAccount *fla, JsonObject *root) {
    PurpleAccount *pa = fla->pa;
    const gchar *character;


    character = json_object_get_string_member(root, "character");
    g_return_val_if_fail(character, TRUE);
    if(!fla->global_ops) fla->global_ops = flist_global_ops_new();

    g_hash_table_remove(fla->global_ops, character);
    flist_update_user_chats_rank(fla, character);

    purple_prpl_got_account_actions(pa);

    if (fla->receive_notifications) {
        gchar *message = g_strdup_printf("%s is no longer a global operator.", character);
        serv_got_im(fla->pc, GLOBAL_NAME, message, PURPLE_MESSAGE_RECV, time(NULL));
        g_free(message);
    }

    return TRUE;
}

void flist_conversation_created_cb(PurpleConversation *conv, FListAccount *fla)
{
    PurpleConnection *pc = purple_conversation_get_gc(conv);

    // Is this a conversation of our account?
    if (fla->pc != pc)
        return;

    if (purple_conversation_get_type(conv) == PURPLE_CONV_TYPE_IM) {
        const char *buddy_name = purple_conversation_get_name(conv);
        flist_temp_im_check(fla, buddy_name);
    }
}

static gboolean flist_process_ADL(FListAccount *fla, JsonObject *root) {
    PurpleAccount *pa = fla->pa;
    JsonArray *ops;
    int i, len;
    GHashTable *old_table;

    ops = json_object_get_array_member(root, "ops");
    g_return_val_if_fail(ops, TRUE);

    old_table = fla->global_ops;

    /* fill in the new table */
    fla->global_ops = flist_global_ops_new();
    len = json_array_get_length(ops);
    for(i = 0; i < len; i++) {
        gchar *identity = g_strdup(json_array_get_string_element(ops, i));
        g_hash_table_insert(fla->global_ops, identity, identity);
    }

    /* update the status of everyone in the old table */
    if(old_table) {
        GList *list = g_hash_table_get_keys(old_table);
        flist_update_users_chats_rank(fla, list);
        g_list_free(list);
        g_hash_table_destroy(old_table);
    }

    /* update the status of everyone in the new table */
    if(fla->global_ops) { //always TRUE
        GList *list = g_hash_table_get_keys(fla->global_ops);
        flist_update_users_chats_rank(fla, list);
        g_list_free(list);
    }

    if (purple_account_is_connected(pa)) purple_prpl_got_account_actions(pa);
    return TRUE;
}

static gboolean flist_process_TPN(FListAccount *fla, JsonObject *root) {
    const gchar *character, *status;

    character = json_object_get_string_member(root, "character");
    status = json_object_get_string_member(root, "status");

    if (!flist_ignore_character_is_ignored(fla, character))
        serv_got_typing(fla->pc, character, 0, flist_typing_state(status));

    return TRUE;
}


static gboolean flist_process_IDN(FListAccount *fla, JsonObject *root) {
    const gchar *character;
    fla->connection_status = FLIST_ONLINE;
    character = json_object_get_string_member(root, "character");
    if (character) {
        if (!flist_str_equal(fla->character, character)) {
            purple_debug_warning(FLIST_DEBUG, "Server sent different character name (%s) from the one configured", character);
        }
        // Even if the character name matches, save the one the server sent us
        // because it will always have the correct case
        g_free(fla->character);
        fla->character = g_strdup(character);
    } else {
        purple_debug_warning(FLIST_DEBUG, "Server did not sent a character name");
    }

    flist_fetch_account_icon(fla);

    return TRUE;
}
static gboolean flist_process_PIN(FListAccount *fla, JsonObject *root) {
    flist_request(fla, "PIN", NULL);
    flist_receive_ping(fla);
    return TRUE;
}

gboolean flist_process_receiving_im(PurpleAccount *account, char **who,
        char **message, int *flags, void *m, FListAccount *fla) {

    g_return_val_if_fail(account, 0);
    g_return_val_if_fail(who, 0);
    g_return_val_if_fail(*who, 0);
    g_return_val_if_fail(message, 0);
    g_return_val_if_fail(*message, 0);
    g_return_val_if_fail(fla, 0);

    // Only for flist IMs, we parse the incoming message into HTML, before
    // Pidgin can print it, this signal handler is called for every protocol, so
    // do not remove this check !
    if (account == fla->pa) {
        gchar *parsed;

        PurpleConversation *convo = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, *who, account);
        if (convo == NULL) {
            convo = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, *who);
        }

        // Free original message and replace it with our parsed version
        parsed = flist_bbcode_to_html(fla, convo, *message);
        g_free(*message);
        *message = parsed;

    }
    return 0;
}


static gboolean flist_process_PRI(FListAccount *fla, JsonObject *root) {
    const gchar *character = json_object_get_string_member(root, "character");
    const gchar *message = json_object_get_string_member(root, "message");

    // BBcode handling is done in flist_process_receiving_im, which is called
    // when serv_got_im sends the receiving-im-msg signal
    if (!flist_ignore_character_is_ignored(fla, character)) {
        serv_got_im(fla->pc, character, message, PURPLE_MESSAGE_RECV, time(NULL));
    }
    return TRUE;
}

gboolean flist_callback(FListAccount *fla, const gchar *code, JsonObject *root) {
    flist_cb_fn callback = g_hash_table_lookup(callbacks, code);
    if(!callback) return TRUE;
    return callback(fla, root);
}
void flist_callback_init() {
    if(callbacks) return;
    callbacks = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);

    g_hash_table_insert(callbacks, "RTB", flist_process_RTB);

    g_hash_table_insert(callbacks, "TPN", flist_process_TPN);

    /* Server variables and info */
    g_hash_table_insert(callbacks, "VAR", flist_process_VAR);
    g_hash_table_insert(callbacks, "HLO", flist_process_HLO);
    g_hash_table_insert(callbacks, "UPT", flist_process_UPT);

    /* info on admins */
    g_hash_table_insert(callbacks, "ADL", flist_process_ADL);
    g_hash_table_insert(callbacks, "AOP", flist_process_AOP);
    g_hash_table_insert(callbacks, "DOP", flist_process_DOP);

    /* admin broadcast */
    g_hash_table_insert(callbacks, "BRO", flist_process_BRO);

    /* system message */
    g_hash_table_insert(callbacks, "SYS", flist_process_SYS);

    /* dice rolls and bottle */
    g_hash_table_insert(callbacks, "RLL", flist_process_RLL);

    /* kink search */
    g_hash_table_insert(callbacks, "FKS", flist_process_FKS);

    g_hash_table_insert(callbacks, "PIN", flist_process_PIN);
    g_hash_table_insert(callbacks, "ERR", flist_process_ERR);

    g_hash_table_insert(callbacks, "IDN", flist_process_IDN);
    g_hash_table_insert(callbacks, "PRI", flist_process_PRI);
    g_hash_table_insert(callbacks, "LIS", flist_process_LIS);
    g_hash_table_insert(callbacks, "CON", flist_process_CON);
    g_hash_table_insert(callbacks, "NLN", flist_process_NLN);
    g_hash_table_insert(callbacks, "STA", flist_process_STA);
    g_hash_table_insert(callbacks, "FLN", flist_process_FLN);

    //profile request
    g_hash_table_insert(callbacks, "PRD", flist_process_PRD);
    g_hash_table_insert(callbacks, "KID", flist_process_KID);

    //channel list callbacks
    g_hash_table_insert(callbacks, "CHA", flist_process_CHA); //public channel list
    g_hash_table_insert(callbacks, "ORS", flist_process_ORS); //private channel list

    //channel event callbacks
    g_hash_table_insert(callbacks, "COL", flist_process_COL); //op list
    g_hash_table_insert(callbacks, "JCH", flist_process_JCH); //join channel
    g_hash_table_insert(callbacks, "LCH", flist_process_LCH); //leave channel
    g_hash_table_insert(callbacks, "CBU", flist_process_CBU); //channel ban
    g_hash_table_insert(callbacks, "CKU", flist_process_CKU); //channel kick
    g_hash_table_insert(callbacks, "CTU", flist_process_CTU); //channel time out
    g_hash_table_insert(callbacks, "ICH", flist_process_ICH); //in channel
    g_hash_table_insert(callbacks, "MSG", flist_process_MSG); //channel message
    g_hash_table_insert(callbacks, "LRP", flist_process_LRP); //channel ad
    g_hash_table_insert(callbacks, "CDS", flist_process_CDS); //channel description
    g_hash_table_insert(callbacks, "RMO", flist_process_RMO); //channel mode
    g_hash_table_insert(callbacks, "CIU", flist_process_CIU); //channel invite

    // Ignore list handling
    g_hash_table_insert(callbacks, "IGN", flist_process_IGN); //ignore list

    //staff call
    g_hash_table_insert(callbacks, "SFC", flist_process_SFC);
}
