#ifndef __SUPER_BUFFER_POOL_H__
#define __SUPER_BUFFER_POOL_H__

#include <gst/gst.h>
#include <glib.h>
#include <pthread.h>

#include "supergbmallocator.h"

typedef struct _SuperBufferPool SuperBufferPool;
typedef struct _SuperStream SuperStream;
typedef struct _CRThreadData CRThreadData;
typedef struct _CRThreadMessage CRThreadMessage;
typedef struct _SuperBufferPoolClass SuperBufferPoolClass;

G_BEGIN_DECLS

#define GST_TYPE_SUPER_BUFFER_POOL (super_buffer_pool_get_type())
#define GST_IS_SUPER_BUFFER_POOL(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), \
                                     GST_TYPE_SUPER_BUFFER_POOL))
#define GST_SUPER_BUFFER_POOL(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
                                   GST_TYPE_SUPER_BUFFER_POOL, SuperBufferPool))
#define GST_SUPER_BUFFER_POOL_CAST(obj) ((SuperBufferPool*)(obj))

struct _SuperBufferPool {
    GstBufferPool parent;

    SuperStream *str;

    /* Capture request related members: */
    GAsyncQueue *processed; // processed CR queue
    GAsyncQueue *pending; // pending CR queue
    GAsyncQueue *inflight; // inflight CR queue
    camera3_stream_t *output_stream;

    gboolean empty;

    GbmAllocator *gbm_allocator;
    GstAllocationParams params;
    guint size;
    GstVideoInfo caps_info;

    gboolean flushing;
    gboolean streaming;

    guint num_queued;
    guint max_latency;
    uint32_t cur_fr; // Used to internally track frame count relative to UMD requests.
    uint32_t cur_fr_sent; // Used to track buffer counter relative to requests sent.

    GstBuffer *buffers[MAX_FRAME];
    guint num_buffs;
};

struct _SuperBufferPoolClass {
    GstBufferPoolClass parent_class;
};

GType super_buffer_pool_get_type();
GstBufferPool *super_buffer_pool_new(SuperStream *str, GstCaps *caps);
gboolean super_buffer_pool_stop_camera(SuperBufferPool *pool);
gboolean supercam_buffer_pool_configure_stream(SuperBufferPool *pool,
    const camera3_device_t *hal_device);
camera3_stream_t *supercam_buffer_pool_get_stream(SuperBufferPool *pool);

camera3_stream_t *super_buffer_pool_get_stream(SuperBufferPool *pool);
G_END_DECLS

#endif /*__SUPER_BUFFER_POOL_H__ */
