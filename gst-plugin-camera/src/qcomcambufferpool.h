#ifndef __QCOM_BUFFER_POOL_H__
#define __QCOM_BUFFER_POOL_H__

#include <gst/gst.h>
#include <glib.h>
#include <pthread.h>

#include "qcomgbmallocator.h"

typedef struct _QcomBufferPool QcomBufferPool;
typedef struct _QcomStream QcomStream;
typedef struct _CRThreadData CRThreadData;
typedef struct _CRThreadMessage CRThreadMessage;
typedef struct _QcomBufferPoolClass QcomBufferPoolClass;

G_BEGIN_DECLS

#define GST_TYPE_QCOM_BUFFER_POOL (qcom_buffer_pool_get_type())
#define GST_IS_QCOM_BUFFER_POOL(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), \
                                     GST_TYPE_QCOM_BUFFER_POOL))
#define GST_QCOM_BUFFER_POOL(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
                                   GST_TYPE_QCOM_BUFFER_POOL, QcomBufferPool))
#define GST_QCOM_BUFFER_POOL_CAST(obj) ((QcomBufferPool*)(obj))

struct _QcomBufferPool {
    GstBufferPool parent;

    QcomStream *str;

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

struct _QcomBufferPoolClass {
    GstBufferPoolClass parent_class;
};

GType qcom_buffer_pool_get_type();
GstBufferPool *qcom_buffer_pool_new(QcomStream *str, GstCaps *caps);
gboolean qcom_buffer_pool_stop_camera(QcomBufferPool *pool);
gboolean qcomcam_buffer_pool_configure_stream(QcomBufferPool *pool,
    const camera3_device_t *hal_device);
camera3_stream_t *qcomcam_buffer_pool_get_stream(QcomBufferPool *pool);

camera3_stream_t *qcom_buffer_pool_get_stream(QcomBufferPool *pool);
G_END_DECLS

#endif /*__QCOM_BUFFER_POOL_H__ */
