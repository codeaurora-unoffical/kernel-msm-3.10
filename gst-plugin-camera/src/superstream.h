#ifndef __SUPERSTREAM_H__
#define __SUPERSTREAM_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <gst/video/video.h>

#include <hardware/camera_common.h>

#include "supergbmallocator.h"
#include "supercambufferpool.h"

G_BEGIN_DECLS

#define SUPER_CAM_MAX_SIZE (1<<15)

#define GST_TYPE_SUPERSTREAM (superstream_get_type())

#define GST_SUPERSTREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SUPERSTREAM, SuperStream))

#define GST_SUPERSTREAM_CLASS(class) \
  (G_TYPE_CHECK_CLASS_CAST((class), GST_TYPE_SUPERSTREAM, SuperStreamClass))

#define GST_IS_SUPERSTREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_SUPERSTREAM))

#define GST_IS_SUPERSTREAM_CLASS(class) \
  (G_TYPE_CHECK_CLASS_TYPE((class), GST_TYPE_SUPERSTREAM))

typedef struct _SuperStream SuperStream;
typedef struct _CallbackOps CallbackOps;
typedef struct _SuperStreamClass SuperStreamClass;

struct _SuperStream {
    GstPushSrc pushsrc;

    SuperHALFormat format;
    GstVideoInfo info;
    GstVideoAlignment align;
    uint32_t usage; // camera3_stream_t stream usage flags..

    gint planes_cnt;
    guint32 min_buffers;

    SuperBufferPool *pool;
    GList *formats;
    GstCaps *probed_caps;

    guint64 frame_cnt;
    GstClockTime ctrl_time;
    int cam_id;
    gpointer cam_bin; /* bp_created_cb private data */
    int (*bp_created_cb)(gpointer cam_bin);
};

struct _SuperStreamClass {
    GstPushSrcClass parent_class;
};

GType superstream_get_type(void);
gboolean super_stream_set_num_buffers(SuperStream *str, const GValue *value);
gint super_stream_get_num_buffers(SuperStream *str);
gboolean super_stream_save_formats(SuperStream *str, GList *formats);
gboolean super_stream_is_linked(SuperStream *str);
GstElement *super_stream_create(const gpointer cam_bin, const gchar *name,
    gboolean (*notify_bp_created)(gpointer cam_bin), int cam_id);

G_END_DECLS

#endif /* __SUPERSTREAM_H__ */
