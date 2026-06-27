#include <stdlib.h>
#include <wayland-client-core.h>

#include "data_control_wrapper.h"
#include "wlr-data-control-protocol.h"
#include "ext-data-control-protocol.h"
#include "xmalloc.h"

// apolocheese 4 macro hell

// macros to generate void wrapper functions
// the argument names are garbage but that shouldn't matter
// because it's never directly accessed
// only through api struct which has sane arg names

// this needs to be separate (i.e. not GEN_WRAP_REQUEST_PARAM0) because of the xmalloc
#define GEN_DESTROY(interface) \
    static void wrap_##interface##_destroy(void *obj) { \
        free(wl_proxy_get_user_data(obj)); \
        interface##_destroy(obj); \
    }

#define GEN_WRAP_REQUEST_PARAM1(interface, name, type1) \
    static void wrap_##interface##_##name(void *data, type1 a) { \
        interface##_##name(data, a); \
    }
#define GEN_WRAP_REQUEST_PARAM2(interface, name, type1, type2) \
    static void wrap_##interface##_##name(void *data, type1 a, type2 b) { \
        interface##_##name(data, a, b); \
    }

// general because this is for both ext and zwlr
#define GEN_DATA_WRAPPER(general_interface_name) \
    struct general_interface_name##_data { \
        void *data; \
        const struct general_interface_name##_listener *listener; \
    };
#define GEN_WRAP_EVENT_PARAM0(interface, general_interface_name, name) \
    static void wrap_##interface##_##name(void *data, struct interface *interface) { \
        struct general_interface_name##_data *wrapdata = data; \
        wrapdata->listener->name(wrapdata->data, interface); \
    }
#define GEN_WRAP_EVENT_PARAM1(interface, general_interface_name, name, type1) \
    static void wrap_##interface##_##name(void *data, struct interface *interface, type1 a) { \
        struct general_interface_name##_data *wrapdata = data; \
        wrapdata->listener->name(wrapdata->data, interface, a); \
    }
#define GEN_WRAP_EVENT_PARAM2(interface, general_interface_name, name, type1, type2) \
    static void wrap_##interface##_##name(void *data, struct interface *interface, type1 a, type2 b) { \
        struct general_interface_name##_data *wrapdata = data; \
        wrapdata->listener->name(wrapdata->data, interface, a, b); \
    }
#define GEN_ADD_LISTENER(interface, general_interface_name) \
    static int wrap_##interface##_add_listener(void *name, const struct general_interface_name##_listener *listener, void *data) { \
        struct general_interface_name##_data *wrapdata = xmalloc(sizeof(*wrapdata)); \
        wrapdata->data = data; \
        wrapdata->listener = listener; \
        return interface##_add_listener(name, &interface##_listener, wrapdata); \
    }

// offer
// request parts
GEN_WRAP_REQUEST_PARAM2(zwlr_data_control_offer_v1, receive, const char *, int32_t)
GEN_WRAP_REQUEST_PARAM2(ext_data_control_offer_v1, receive, const char *, int32_t)
GEN_DESTROY(zwlr_data_control_offer_v1)
GEN_DESTROY(ext_data_control_offer_v1)
// event parts
GEN_DATA_WRAPPER(offer)
GEN_WRAP_EVENT_PARAM1(zwlr_data_control_offer_v1, offer, offer, const char *)
GEN_WRAP_EVENT_PARAM1(ext_data_control_offer_v1, offer, offer, const char *)
#define GEN_OFFER_LISTENER(interface) \
    static struct interface##_listener interface##_listener = { \
        .offer = wrap_##interface##_offer, \
    };
GEN_OFFER_LISTENER(zwlr_data_control_offer_v1)
GEN_OFFER_LISTENER(ext_data_control_offer_v1)
GEN_ADD_LISTENER(zwlr_data_control_offer_v1, offer)
GEN_ADD_LISTENER(ext_data_control_offer_v1, offer)

// source
// request parts
GEN_WRAP_REQUEST_PARAM1(zwlr_data_control_source_v1, offer, const char *)
GEN_WRAP_REQUEST_PARAM1(ext_data_control_source_v1, offer, const char *)
GEN_DESTROY(zwlr_data_control_source_v1)
GEN_DESTROY(ext_data_control_source_v1)
// event parts
GEN_DATA_WRAPPER(source)
GEN_WRAP_EVENT_PARAM2(zwlr_data_control_source_v1, source, send, const char *, int32_t)
GEN_WRAP_EVENT_PARAM2(ext_data_control_source_v1, source, send, const char *, int32_t)
GEN_WRAP_EVENT_PARAM0(zwlr_data_control_source_v1, source, cancelled)
GEN_WRAP_EVENT_PARAM0(ext_data_control_source_v1, source, cancelled)
#define GEN_SOURCE_LISTENER(interface) \
    static struct interface##_listener interface##_listener = { \
        .send = wrap_##interface##_send, \
        .cancelled = wrap_##interface##_cancelled, \
    };
GEN_SOURCE_LISTENER(zwlr_data_control_source_v1)
GEN_SOURCE_LISTENER(ext_data_control_source_v1)
GEN_ADD_LISTENER(zwlr_data_control_source_v1, source)
GEN_ADD_LISTENER(ext_data_control_source_v1, source)

// device
// request parts
GEN_WRAP_REQUEST_PARAM1(zwlr_data_control_device_v1, set_selection, void *)
GEN_WRAP_REQUEST_PARAM1(ext_data_control_device_v1, set_selection, void *)
GEN_WRAP_REQUEST_PARAM1(zwlr_data_control_device_v1, set_primary_selection, void *)
GEN_WRAP_REQUEST_PARAM1(ext_data_control_device_v1, set_primary_selection, void *)
GEN_DESTROY(zwlr_data_control_device_v1)
GEN_DESTROY(ext_data_control_device_v1)
// event parts
GEN_DATA_WRAPPER(device)
GEN_WRAP_EVENT_PARAM1(zwlr_data_control_device_v1, device, data_offer, struct zwlr_data_control_offer_v1 *)
GEN_WRAP_EVENT_PARAM1(ext_data_control_device_v1, device, data_offer, struct ext_data_control_offer_v1 *)
GEN_WRAP_EVENT_PARAM1(zwlr_data_control_device_v1, device, selection, struct zwlr_data_control_offer_v1 *)
GEN_WRAP_EVENT_PARAM1(ext_data_control_device_v1, device, selection, struct ext_data_control_offer_v1 *)
GEN_WRAP_EVENT_PARAM1(zwlr_data_control_device_v1, device, primary_selection, struct zwlr_data_control_offer_v1 *)
GEN_WRAP_EVENT_PARAM1(ext_data_control_device_v1, device, primary_selection, struct ext_data_control_offer_v1 *)
GEN_WRAP_EVENT_PARAM0(zwlr_data_control_device_v1, device, finished)
GEN_WRAP_EVENT_PARAM0(ext_data_control_device_v1, device, finished)
#define GEN_DEVICE_LISTENER(interface) \
    static struct interface##_listener interface##_listener = { \
        .data_offer = wrap_##interface##_data_offer, \
        .selection = wrap_##interface##_selection, \
        .primary_selection = wrap_##interface##_primary_selection, \
        .finished = wrap_##interface##_finished, \
    };
GEN_DEVICE_LISTENER(zwlr_data_control_device_v1)
GEN_DEVICE_LISTENER(ext_data_control_device_v1)
GEN_ADD_LISTENER(zwlr_data_control_device_v1, device)
GEN_ADD_LISTENER(ext_data_control_device_v1, device)

// device manager
// request parts
// we can't reuse most of the GEN_ because these return values (lame...)
#define GEN_CREATE_DATA_SOURCE(interface) \
    static void *wrap_##interface##_create_data_source(void *manager) { \
        return interface##_create_data_source(manager); \
    }
GEN_CREATE_DATA_SOURCE(zwlr_data_control_manager_v1)
GEN_CREATE_DATA_SOURCE(ext_data_control_manager_v1)
#define GEN_GET_DATA_DEVICE(interface) \
    static void *wrap_##interface##_get_data_device(void *manager, struct wl_seat *seat) { \
        return interface##_get_data_device(manager, seat); \
    }
GEN_GET_DATA_DEVICE(zwlr_data_control_manager_v1)
GEN_GET_DATA_DEVICE(ext_data_control_manager_v1)
GEN_DESTROY(zwlr_data_control_manager_v1)
GEN_DESTROY(ext_data_control_manager_v1)

#define GEN_API(prefix) \
    static struct dc_api prefix##_api = { \
        .offer_receive = wrap_##prefix##_data_control_offer_v1_receive, \
        .offer_destroy = wrap_##prefix##_data_control_offer_v1_destroy, \
        .offer_add_listener = wrap_##prefix##_data_control_offer_v1_add_listener, \
        .source_offer = wrap_##prefix##_data_control_source_v1_offer, \
        .source_destroy = wrap_##prefix##_data_control_source_v1_destroy, \
        .source_add_listener = wrap_##prefix##_data_control_source_v1_add_listener, \
        .device_set_selection = wrap_##prefix##_data_control_device_v1_set_selection, \
        .device_set_primary_selection = wrap_##prefix##_data_control_device_v1_set_primary_selection, \
        .device_destroy = wrap_##prefix##_data_control_device_v1_destroy, \
        .device_add_listener = wrap_##prefix##_data_control_device_v1_add_listener, \
        .manager_create_data_source = wrap_##prefix##_data_control_manager_v1_create_data_source, \
        .manager_get_data_device = wrap_##prefix##_data_control_manager_v1_get_data_device, \
        .manager_destroy = wrap_##prefix##_data_control_manager_v1_destroy, \
    };
GEN_API(zwlr)
GEN_API(ext)

struct dc_api data_control = {0};

void set_api_zwlr(void) {
    data_control = zwlr_api;
}

void set_api_ext(void) {
    data_control = ext_api;
}
