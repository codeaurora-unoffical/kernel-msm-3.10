#ifndef __QCOMHAL3SRC_H__
#define __QCOMHAL3SRC_H__

#include "qcomhal3cam.h"

G_BEGIN_DECLS

#define GST_TYPE_QCOMHAL3SRC (qcomhal3src_get_type())

#define GST_QCOMHAL3SRC(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj, GST_TYPE_QCOMHAL3SRC, QcomHal3Src))

#define GST_QCOMHAL3SRC_CAST(obj)   ((QcomHal3Src *) obj)

#define GST_QCOMHAL3SRC_CLASS(class) \
    (G_TYPE_CHECK_CLASS_CAST((obj), GST_TYPE_QCOMHAL3SRC, QcomHal3SrcClass))

#define GST_IS_QCOMHAL3SRC(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_QCOMHAL3SRC))

#define GST_IS_QCOMHAL3SRC_CLASS(class) \
    (G_TYPE_CHECK_CLASS_TYPE((class), GST_TYPE_QCOMHAL3SRC))

#define MAX_CAMS 4

typedef struct _QcomHal3Src QcomHal3Src;
typedef struct _QcomHal3SrcClass QcomHal3SrcClass;

struct _QcomHal3Src {
    GstBin bin;

    GstElement *cameras[MAX_CAMS];
    guint num_cameras;
    GstPad *proxy_pads[MAX_CAMS][MAX_STREAMS];
};

struct _QcomHal3SrcClass {
    GstBinClass bin_class;
};

GType qcomhal3src_get_type(void);

G_END_DECLS
#endif /* __QCOMHAL3SRC_H__ */
