#ifndef __QCOMSTREAM_H__
#define __QCOMSTREAM_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <gst/video/video.h>

#include <hardware/camera_common.h>

#include "qcomgbmallocator.h"
#include "qcomcambufferpool.h"

G_BEGIN_DECLS

#define QCOM_CAM_MAX_SIZE (1<<15)

#define GST_TYPE_QCOMSTREAM (qcomstream_get_type())

#define GST_QCOMSTREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_QCOMSTREAM, QcomStream))

#define GST_QCOMSTREAM_CLASS(class) \
  (G_TYPE_CHECK_CLASS_CAST((class), GST_TYPE_QCOMSTREAM, QcomStreamClass))

#define GST_IS_QCOMSTREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_QCOMSTREAM))

#define GST_IS_QCOMSTREAM_CLASS(class) \
  (G_TYPE_CHECK_CLASS_TYPE((class), GST_TYPE_QCOMSTREAM))

typedef struct _QcomStream QcomStream;
typedef struct _CallbackOps CallbackOps;
typedef struct _QcomStreamClass QcomStreamClass;

struct _QcomStream {
    GstPushSrc pushsrc;

    QcomHALFormat format;
    GstVideoInfo info;
    GstVideoAlignment align;
    uint32_t usage; // camera3_stream_t stream usage flags..

    gint planes_cnt;
    guint32 min_buffers;

    QcomBufferPool *pool;
    GList *formats;
    GstCaps *probed_caps;

    guint64 frame_cnt;
    GstClockTime ctrl_time;
    int cam_id;
    gpointer cam_bin; /* bp_created_cb private data */
    int (*bp_created_cb)(gpointer cam_bin);
};

struct _QcomStreamClass {
    GstPushSrcClass parent_class;
};

GType qcomstream_get_type(void);
gboolean qcom_stream_set_num_buffers(QcomStream *str, const GValue *value);
gint qcom_stream_get_num_buffers(QcomStream *str);
gboolean qcom_stream_save_formats(QcomStream *str, GList *formats);
gboolean qcom_stream_is_linked(QcomStream *str);
GstElement *qcom_stream_create(const gpointer cam_bin, const gchar *name,
    gboolean (*notify_bp_created)(gpointer cam_bin), int cam_id);

G_END_DECLS

#endif /* __QCOMSTREAM_H__ */
