#ifndef __SUPERHAL3SRC_H__
#define __SUPERHAL3SRC_H__

#include "superhal3cam.h"

G_BEGIN_DECLS

#define GST_TYPE_SUPERHAL3SRC (superhal3src_get_type())

#define GST_SUPERHAL3SRC(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj, GST_TYPE_SUPERHAL3SRC, SuperHal3Src))

#define GST_SUPERHAL3SRC_CAST(obj)   ((SuperHal3Src *) obj)

#define GST_SUPERHAL3SRC_CLASS(class) \
    (G_TYPE_CHECK_CLASS_CAST((obj), GST_TYPE_SUPERHAL3SRC, SuperHal3SrcClass))

#define GST_IS_SUPERHAL3SRC(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_SUPERHAL3SRC))

#define GST_IS_SUPERHAL3SRC_CLASS(class) \
    (G_TYPE_CHECK_CLASS_TYPE((class), GST_TYPE_SUPERHAL3SRC))

#define MAX_CAMS 4

typedef struct _SuperHal3Src SuperHal3Src;
typedef struct _SuperHal3SrcClass SuperHal3SrcClass;

struct _SuperHal3Src {
    GstBin bin;

    GstElement *cameras[MAX_CAMS];
    guint num_cameras;
    GstPad *proxy_pads[MAX_CAMS][MAX_STREAMS];
};

struct _SuperHal3SrcClass {
    GstBinClass bin_class;
};

GType superhal3src_get_type(void);

G_END_DECLS
#endif /* __SUPERHAL3SRC_H__ */
