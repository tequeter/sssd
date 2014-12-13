/*
    Authors:
        Pavel Březina <pbrezina@redhat.com>

    Copyright (C) 2014 Red Hat

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <talloc.h>
#include <dbus/dbus.h>
#include <dhash.h>

#include "util/util.h"
#include "sbus/sssd_dbus.h"
#include "sbus/sssd_dbus_meta.h"
#include "sbus/sssd_dbus_private.h"

static struct sbus_interface *
sbus_iface_list_lookup(struct sbus_interface_list *list,
                       const char *iface)
{
    struct sbus_interface_list *item;

    DLIST_FOR_EACH(item, list) {
        if (strcmp(item->interface->vtable->meta->name, iface) == 0) {
            return item->interface;
        }
    }

    return NULL;
}

static errno_t
sbus_iface_list_copy(TALLOC_CTX *mem_ctx,
                     struct sbus_interface_list *list,
                     struct sbus_interface_list **_copy)
{
    TALLOC_CTX *list_ctx;
    struct sbus_interface_list *new_list = NULL;
    struct sbus_interface_list *new_item;
    struct sbus_interface_list *item;
    errno_t ret;

    if (list == NULL) {
        *_copy = NULL;
        return EOK;
    }

    list_ctx = talloc_new(mem_ctx);
    if (list_ctx == NULL) {
        return ENOMEM;
    }

    DLIST_FOR_EACH(item, list) {
        if (sbus_iface_list_lookup(new_list,
               item->interface->vtable->meta->name) != NULL) {
            /* already in list */
            continue;
        }

        new_item = talloc_zero(list_ctx, struct sbus_interface_list);
        if (new_item == NULL) {
            ret = ENOMEM;
            goto done;
        }

        new_item->interface = item->interface;
        DLIST_ADD(new_list, new_item);
    }

    *_copy = new_list;
    ret = EOK;

done:
    if (ret != EOK) {
        talloc_free(list_ctx);
    }

    return ret;
}

/**
 * Object paths that represent all objects under the path:
 * /org/object/path/~* (without tilda)
 */
static bool sbus_opath_is_subtree(const char *path)
{
    size_t len;

    len = strlen(path);

    if (len < 2) {
        return false;
    }

    return path[len - 2] == '/' && path[len - 1] == '*';
}

/**
 * If the path represents a subtree object path, this function will
 * remove /~* from the end.
 */
static char *sbus_opath_get_base_path(TALLOC_CTX *mem_ctx,
                                      const char *object_path)
{
    char *tree_path;
    size_t len;

    tree_path = talloc_strdup(mem_ctx, object_path);
    if (tree_path == NULL) {
        return NULL;
    }

    if (!sbus_opath_is_subtree(tree_path)) {
        return tree_path;
    }

    /* replace / only if it is not a root path (only slash) */
    len = strlen(tree_path);
    tree_path[len - 1] = '\0';
    tree_path[len - 2] = (len - 2 != 0) ? '\0' : '/';

    return tree_path;
}

static char *sbus_opath_parent_subtree(TALLOC_CTX *mem_ctx,
                                       const char *path)
{
    char *subtree;
    char *slash;

    /* first remove /~* from the end, stop when we have reached the root i.e.
     * subtree == "/" */
    subtree = sbus_opath_get_base_path(mem_ctx, path);
    if (subtree == NULL || subtree[1] == '\0') {
        return NULL;
    }

    /* Find the first separator and replace the part with asterisk. */
    slash = strrchr(subtree, '/');
    if (slash == NULL) {
        /* we cannot continue up */
        talloc_free(subtree);
        return NULL;
    }

    if (*(slash + 1) == '\0') {
        /* this object path is invalid since it cannot end with slash */
        DEBUG(SSSDBG_CRIT_FAILURE, "Invalid object path '%s'?\n", path);
        talloc_free(subtree);
        return NULL;
    }

    /* because object path cannot end with / there is enough space for
     * asterisk and terminating zero */
    *(slash + 1) = '*';
    *(slash + 2) = '\0';

    return subtree;
}

static void
sbus_opath_hash_delete_cb(hash_entry_t *item,
                          hash_destroy_enum deltype,
                          void *pvt)
{
    struct sbus_connection *conn;
    char *path;

    conn = talloc_get_type(pvt, struct sbus_connection);
    path = sbus_opath_get_base_path(NULL, item->key.str);

    dbus_connection_unregister_object_path(conn->dbus.conn, path);
}

errno_t
sbus_opath_hash_init(TALLOC_CTX *mem_ctx,
                     struct sbus_connection *conn,
                     hash_table_t **_table)
{
    return sss_hash_create_ex(mem_ctx, 10, _table, 0, 0, 0, 0,
                              sbus_opath_hash_delete_cb, conn);
}

static errno_t
sbus_opath_hash_add_iface(hash_table_t *table,
                          const char *object_path,
                          struct sbus_interface *iface,
                          bool *_path_known)
{
    TALLOC_CTX *tmp_ctx = NULL;
    struct sbus_interface_list *list = NULL;
    struct sbus_interface_list *item = NULL;
    const char *iface_name = iface->vtable->meta->name;
    hash_key_t key;
    hash_value_t value;
    bool path_known;
    errno_t ret;
    int hret;

    tmp_ctx = talloc_new(NULL);
    if (tmp_ctx == NULL) {
        return ENOMEM;
    }

    DEBUG(SSSDBG_TRACE_FUNC, "Registering interface %s with path %s\n",
          iface_name, object_path);

    /* create new list item */

    item = talloc_zero(tmp_ctx, struct sbus_interface_list);
    if (item == NULL) {
        return ENOMEM;
    }

    item->interface = iface;

    /* first lookup existing list in hash table */

    key.type = HASH_KEY_STRING;
    key.str = talloc_strdup(tmp_ctx, object_path);
    if (key.str == NULL) {
        ret = ENOMEM;
        goto done;
    }

    hret = hash_lookup(table, &key, &value);
    if (hret == HASH_SUCCESS) {
        /* This object path has already some interface registered. We will
         * check for existence of the interface currently being added and
         * add it if missing. */

        path_known = true;

        list = talloc_get_type(value.ptr, struct sbus_interface_list);
        if (sbus_iface_list_lookup(list, iface_name) != NULL) {
            DEBUG(SSSDBG_MINOR_FAILURE, "Trying to register the same interface"
                  " twice: iface=%s, opath=%s\n", iface_name, object_path);
            ret = EEXIST;
            goto done;
        }

        DLIST_ADD_END(list, item, struct sbus_interface_list *);
        ret = EOK;
        goto done;
    } else if (hret != HASH_ERROR_KEY_NOT_FOUND) {
        ret = EIO;
        goto done;
    }

    /* otherwise create new hash entry and new list */

    path_known = false;
    list = item;

    value.type = HASH_VALUE_PTR;
    value.ptr = list;

    hret = hash_enter(table, &key, &value);
    if (hret != HASH_SUCCESS) {
        ret = EIO;
        goto done;
    }

    talloc_steal(table, key.str);
    ret = EOK;

done:
    if (ret == EOK) {
        talloc_steal(item, iface);
        talloc_steal(table, item);
        *_path_known = path_known;
    } else {
        talloc_free(item);
    }

    return ret;
}

static bool
sbus_opath_hash_has_path(hash_table_t *table,
                         const char *object_path)
{
    hash_key_t key;

    key.type = HASH_KEY_STRING;
    key.str = discard_const(object_path);

    return hash_has_key(table, &key);
}

/**
 * First @object_path is looked up in @table, if it is not found it steps up
 * in the path hierarchy and try to lookup the parent node. This continues
 * until the root is reached.
 */
static struct sbus_interface *
sbus_opath_hash_lookup_iface(hash_table_t *table,
                             const char *object_path,
                             const char *iface_name)
{
    TALLOC_CTX *tmp_ctx = NULL;
    struct sbus_interface_list *list = NULL;
    struct sbus_interface *iface = NULL;
    char *lookup_path = NULL;
    hash_key_t key;
    hash_value_t value;
    int hret;

    tmp_ctx = talloc_new(NULL);
    if (tmp_ctx == NULL) {
        return NULL;
    }

    lookup_path = talloc_strdup(tmp_ctx, object_path);
    if (lookup_path == NULL) {
        goto done;
    }

    while (lookup_path != NULL) {
        key.type = HASH_KEY_STRING;
        key.str = lookup_path;

        hret = hash_lookup(table, &key, &value);
        if (hret == HASH_SUCCESS) {
            list = talloc_get_type(value.ptr, struct sbus_interface_list);
            iface = sbus_iface_list_lookup(list, iface_name);
            if (iface != NULL) {
                goto done;
            }
        } else if (hret != HASH_ERROR_KEY_NOT_FOUND) {
            DEBUG(SSSDBG_OP_FAILURE,
                  "Unable to search hash table: hret=%d\n", hret);
            iface = NULL;
            goto done;
        }

        /* we will not free lookup path since it is freed with tmp_ctx
         * and the object paths are supposed to be small */
        lookup_path = sbus_opath_parent_subtree(tmp_ctx, lookup_path);
    }

done:
    talloc_free(tmp_ctx);
    return iface;
}

/**
 * Acquire list of all interfaces that are supported on given object path.
 */
errno_t
sbus_opath_hash_lookup_supported(TALLOC_CTX *mem_ctx,
                                 hash_table_t *table,
                                 const char *object_path,
                                 struct sbus_interface_list **_list)
{
    TALLOC_CTX *tmp_ctx = NULL;
    TALLOC_CTX *list_ctx = NULL;
    struct sbus_interface_list *copy = NULL;
    struct sbus_interface_list *list = NULL;
    char *lookup_path = NULL;
    hash_key_t key;
    hash_value_t value;
    errno_t ret;
    int hret;

    tmp_ctx = talloc_new(NULL);
    if (tmp_ctx == NULL) {
        return ENOMEM;
    }

    list_ctx = talloc_new(tmp_ctx);
    if (list_ctx == NULL) {
        ret = ENOMEM;
        goto done;
    }

    lookup_path = talloc_strdup(tmp_ctx, object_path);
    if (lookup_path == NULL) {
        ret = ENOMEM;
        goto done;
    }

    while (lookup_path != NULL) {
        key.type = HASH_KEY_STRING;
        key.str = lookup_path;

        hret = hash_lookup(table, &key, &value);
        if (hret == HASH_SUCCESS) {
            ret = sbus_iface_list_copy(list_ctx, value.ptr, &copy);
            if (ret != EOK) {
                goto done;
            }

            DLIST_CONCATENATE(list, copy, struct sbus_interface_list *);
        } else if (hret != HASH_ERROR_KEY_NOT_FOUND) {
            DEBUG(SSSDBG_OP_FAILURE,
                  "Unable to search hash table: hret=%d\n", hret);
            ret = EIO;
            goto done;
        }

        /* we will not free lookup path since it is freed with tmp_ctx
         * and the object paths are supposed to be small */
        lookup_path = sbus_opath_parent_subtree(tmp_ctx, lookup_path);
    }

    talloc_steal(mem_ctx, list_ctx);
    *_list = list;
    ret = EOK;

done:
    talloc_free(tmp_ctx);
    return ret;
}

static struct sbus_interface *
sbus_new_interface(TALLOC_CTX *mem_ctx,
                   const char *object_path,
                   struct sbus_vtable *iface_vtable,
                   void *instance_data)
{
    struct sbus_interface *intf;

    intf = talloc_zero(mem_ctx, struct sbus_interface);
    if (intf == NULL) {
        DEBUG(SSSDBG_FATAL_FAILURE, "Cannot allocate a new sbus_interface.\n");
        return NULL;
    }

    intf->path = talloc_strdup(intf, object_path);
    if (intf->path == NULL) {
        DEBUG(SSSDBG_FATAL_FAILURE, "Cannot duplicate object path.\n");
        talloc_free(intf);
        return NULL;
    }

    intf->vtable = iface_vtable;
    intf->instance_data = instance_data;
    return intf;
}

static DBusHandlerResult
sbus_message_handler(DBusConnection *dbus_conn,
                     DBusMessage *message,
                     void *user_data);

static errno_t
sbus_conn_register_path(struct sbus_connection *conn,
                        const char *path)
{
    static DBusObjectPathVTable vtable = {NULL, sbus_message_handler,
                                          NULL, NULL, NULL, NULL};
    DBusError error;
    char *reg_path = NULL;
    dbus_bool_t dbret;

    DEBUG(SSSDBG_TRACE_FUNC, "Registering object path %s with D-Bus "
          "connection\n", path);

    if (sbus_opath_is_subtree(path)) {
        reg_path = sbus_opath_get_base_path(conn, path);
        if (reg_path == NULL) {
            return ENOMEM;
        }

        /* D-Bus does not allow to have both object path and fallback
         * registered. Since we handle the real message handlers ourselves
         * we will register fallback only in this case. */
        if (sbus_opath_hash_has_path(conn->managed_paths, reg_path)) {
            dbus_connection_unregister_object_path(conn->dbus.conn, reg_path);
        }

        dbret = dbus_connection_register_fallback(conn->dbus.conn, reg_path,
                                                  &vtable, conn);
        talloc_free(reg_path);
    } else {
        dbus_error_init(&error);

        dbret = dbus_connection_try_register_object_path(conn->dbus.conn, path,
                                                         &vtable, conn, &error);

        if (dbus_error_is_set(&error) &&
                strcmp(error.name, DBUS_ERROR_OBJECT_PATH_IN_USE) == 0) {
            /* A fallback is probably already registered. Just return. */
            dbus_error_free(&error);
            return EOK;
        }
    }

    if (!dbret) {
        DEBUG(SSSDBG_FATAL_FAILURE, "Unable to register object path "
              "%s with D-Bus connection.\n", path);
        return ENOMEM;
    }

    return EOK;
}

errno_t
sbus_conn_register_iface(struct sbus_connection *conn,
                         struct sbus_vtable *iface_vtable,
                         const char *object_path,
                         void *pvt)
{
    struct sbus_interface *iface = NULL;
    bool path_known;
    errno_t ret;

    if (conn == NULL || iface_vtable == NULL || object_path == NULL) {
        return EINVAL;
    }

    iface = sbus_new_interface(conn, object_path, iface_vtable, pvt);
    if (iface == NULL) {
        return ENOMEM;
    }

    ret = sbus_opath_hash_add_iface(conn->managed_paths, object_path, iface,
                                    &path_known);
    if (ret != EOK) {
        talloc_free(iface);
        return ret;
    }

    if (path_known) {
        /* this object path is already registered */
        return EOK;
    }

    /* if ret != EOK we will still leave iface in the table, since
     * we probably don't have enough memory to remove it correctly anyway */

    ret = sbus_conn_register_path(conn, object_path);
    if (ret != EOK) {
        return ret;
    }

    /* register standard interfaces with this object path as well */
    ret = sbus_conn_register_iface(conn, sbus_introspect_vtable(),
                                   object_path, conn);

    return ret;
}

errno_t
sbus_conn_reregister_paths(struct sbus_connection *conn)
{
    hash_key_t *keys = NULL;
    unsigned long count;
    unsigned long i;
    errno_t ret;
    int hret;

    hret = hash_keys(conn->managed_paths, &count, &keys);
    if (hret != HASH_SUCCESS) {
        ret = ENOMEM;
        goto done;
    }

    for (i = 0; i < count; i++) {
        ret = sbus_conn_register_path(conn, keys[i].str);
        if (ret != EOK) {
            goto done;
        }
    }

    ret = EOK;

done:
    talloc_free(keys);
    return ret;
}

static void
sbus_message_handler_got_caller_id(struct tevent_req *req);

static DBusHandlerResult
sbus_message_handler(DBusConnection *dbus_conn,
                     DBusMessage *message,
                     void *user_data)
{
    struct tevent_req *req;
    struct sbus_connection *conn;
    struct sbus_interface *iface;
    struct sbus_request *sbus_req;
    const struct sbus_method_meta *method;
    const char *iface_name;
    const char *method_name;
    const char *path;
    const char *sender;

    conn = talloc_get_type(user_data, struct sbus_connection);

    /* header information */
    iface_name = dbus_message_get_interface(message);
    method_name = dbus_message_get_member(message);
    path = dbus_message_get_path(message);
    sender = dbus_message_get_sender(message);

    DEBUG(SSSDBG_TRACE_INTERNAL, "Received SBUS method %s.%s on path %s\n",
          iface_name, method_name, path);

    /* try to find the interface */
    iface = sbus_opath_hash_lookup_iface(conn->managed_paths,
                                         path, iface_name);
    if (iface == NULL) {
        goto fail;
    }

    method = sbus_meta_find_method(iface->vtable->meta, method_name);
    if (method == NULL || method->vtable_offset == 0) {
        goto fail;
    }

    /* we have a valid handler, create D-Bus request */
    sbus_req = sbus_new_request(conn, iface, message);
    if (sbus_req == NULL) {
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }

    sbus_req->method = method;

    /* now get the sender ID */
    req = sbus_get_sender_id_send(sbus_req, conn->ev, conn, sender);
    if (req == NULL) {
        talloc_free(sbus_req);
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    tevent_req_set_callback(req, sbus_message_handler_got_caller_id, sbus_req);

    return DBUS_HANDLER_RESULT_HANDLED;

fail: ;
    DBusMessage *reply;

    DEBUG(SSSDBG_CRIT_FAILURE, "No matching handler found for method %s.%s "
          "on path %s\n", iface_name, method_name, path);

    reply = dbus_message_new_error(message, DBUS_ERROR_UNKNOWN_METHOD, NULL);
    sbus_conn_send_reply(conn, reply);

    return DBUS_HANDLER_RESULT_HANDLED;
}

static void
sbus_message_handler_got_caller_id(struct tevent_req *req)
{
    struct sbus_request *sbus_req;
    const struct sbus_method_meta *method;
    sbus_msg_handler_fn handler;
    sbus_method_invoker_fn invoker;
    void *pvt;
    DBusError *error;
    errno_t ret;

    sbus_req = tevent_req_callback_data(req, struct sbus_request);
    method = sbus_req->method;

    ret = sbus_get_sender_id_recv(req, &sbus_req->client);
    if (ret != EOK) {
        error = sbus_error_new(sbus_req, DBUS_ERROR_FAILED, "Failed to "
                               "resolve caller's ID: %s\n", sss_strerror(ret));
        sbus_request_fail_and_finish(sbus_req, error);
        return;
    }

    handler = VTABLE_FUNC(sbus_req->intf->vtable, method->vtable_offset);
    invoker = method->invoker;
    pvt = sbus_req->intf->instance_data;

    sbus_request_invoke_or_finish(sbus_req, handler, pvt, invoker);
    return;
}