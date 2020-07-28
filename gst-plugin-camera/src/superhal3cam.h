#ifndef __SUPERHAL3CAM_H__
#define __SUPERHAL3CAM_H__

#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include "superstream.h"

G_BEGIN_DECLS

#define GST_TYPE_SUPERHAL3CAM (superhal3cam_get_type())

#define GST_SUPERHAL3CAM(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj, GST_TYPE_SUPERHAL3CAM, SuperHal3Cam))

#define GST_SUPERHAL3CAM_CAST(obj)   ((SuperHal3Cam *) obj)

#define GST_SUPERHAL3CAM_CLASS(class) \
    (G_TYPE_CHECK_CLASS_CAST((obj), GST_TYPE_SUPERHAL3CAM, SuperHal3CamClass))

#define GST_IS_SUPERHAL3CAM(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_SUPERHAL3CAM))

#define GST_IS_SUPERHAL3CAM_CLASS(class) \
    (G_TYPE_CHECK_CLASS_TYPE((class), GST_TYPE_SUPERHAL3CAM))

#define NAME_LEN 256
#define MAX_STREAMS 1

#define CAMERA_HAL_IS_OPEN(o)      ((o)->module)

typedef struct _SuperHal3Cam SuperHal3Cam;
typedef struct _SuperHal3CamClass SuperHal3CamClass;

struct _CallbackOps {
    camera3_callback_ops_t ops; /* must be first member in struct! */
    SuperHal3Cam *parent;
};

typedef enum {
    STOP,
    REQUEST_CHANGE,
} CRThreadMsgType;

struct _CRThreadMessage {
    CRThreadMsgType t;
    gpointer data; /* not used, reserved for future use */
};

struct _CRThreadData {
    GAsyncQueue *msg_q;
    SuperBufferPool *pools[MAX_STREAMS];
    pthread_mutex_t device_lock;
    const camera3_device_t *device; /* serialize all access to this! */
    guint num_streams;
};

struct _SuperHal3Cam {
    GstBin bin;

    GstElement *streams[MAX_STREAMS];
    guint num_streams;

    GList *formats;

    // camera HAL members
    int cam_id;
    camera_module_t *module;
    camera3_device_t *device;
    CallbackOps cb_ops;

    /* Capture request related members: */
    pthread_t cr_thread;
    CRThreadData crt_data;

    struct {
        pthread_mutex_t lock;
        pthread_cond_t wait_pools_created;
        unsigned created_pools;
    } pushsrc_sync;
};

struct _SuperHal3CamClass {
    GstBinClass bin_class;
};

GType superhal3cam_get_type(void);

G_END_DECLS
#endif /* __SUPERHAL3CAM_H__ */
