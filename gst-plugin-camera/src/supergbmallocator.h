#ifndef __SUPER_GBM_ALLOCATOR_H__
#define __SUPER_GBM_ALLOCATOR_H__

#include <gst/gst.h>
#include <gst/gstatomicqueue.h>

#include <hardware/camera3.h>
#include <system/camera_metadata.h>
#include <system/graphics.h>
#include <gbm.h>
#include <hardware/gralloc1.h>


G_BEGIN_DECLS

#define GBM_DEVICE "/dev/dri/card0"

#define GST_TYPE_GBM_ALLOCATOR (gbm_allocator_get_type())
#define GST_IS_GBM_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                                   GST_TYPE_GBM_ALLOCATOR))
#define GST_IS_GBM_ALLOCATOR_CLASS(class) (G_TYPE_CHECK_CLASS_TYPE ((class), \
                                           GST_TYPE_GBM_ALLOCATOR))
#define GBM_ALLOCATOR_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
                                      GST_TYPE_GBM_ALLOCATOR, GbmAllocatorClass))
#define GBM_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                            GST_TYPE_GBM_ALLOCATOR, GbmAllocator))
#define GBM_ALLOCATOR_CLASS(class) (G_TYPE_CHECK_CLASS_CAST ((class), \
                                    GST_TYPE_GBM_ALLOCATOR, GbmAllocatorClass))
#define GBM_ALLOCATOR_CAST(obj) ((GbmAllocator *)(obj))

#define MAX_PLANES (8)
#define MAX_FRAME  (32)

typedef struct _GbmAllocator GbmAllocator;
typedef struct _GbmAllocatorClass GbmAllocatorClass;
typedef struct _SuperMemoryGroup SuperMemoryGroup;
typedef struct _SuperMemory SuperMemory;
typedef struct _SuperHALFormat SuperHALFormat;
typedef enum _AllocResult AllocResult;


enum _AllocResult {
    ALLOC_OK    =  0,
    ALLOC_ERROR = -1,
    ALLOC_BUSY  = -2
};

struct _SuperHALFormat {
    guint32 width;
    guint32 height;
    guint32 stride[MAX_PLANES];
    guint32 plane_size[MAX_PLANES];
    guint32 total_size;
    guint32 planes_cnt;
    android_pixel_format_t pix_fmt;
};

struct _SuperMemory {
    GstMemory mem;
    gint plane;
    SuperMemoryGroup *group;
    gpointer data;
    int size;
    guint buf_idx;
};

struct _SuperMemoryGroup {
    GstMemory *mem;
    gint mems_allocated;
    buffer_handle_t buffer;
    guint buf_idx;
    camera3_stream_buffer_t *stream_buffer;
};

struct _GbmAllocator {
    GstAllocator parent;
    SuperHALFormat format;
    gboolean active;

    SuperMemoryGroup *groups[MAX_FRAME];
    guint32 count;
    GstAtomicQueue *queue;
    // libGBM members:
    int gbm_dev_fd;
    struct gbm_device *gbm_dev;
};

struct _GbmAllocatorClass {
    GstAllocatorClass parent_class;
};

GType gbm_allocator_get_type();
GQuark super_memory_quark();
GbmAllocator *gbm_allocator_new(GstObject *parent, SuperHALFormat *format);
void gbm_allocator_start(GbmAllocator *allocator, guint32 count);
AllocResult gbm_allocator_stop(GbmAllocator *allocator);
void gbm_allocator_flush (GbmAllocator *allocator);
void gbm_allocator_prepare_buf(GbmAllocator *allocator, SuperMemoryGroup *group);
SuperMemoryGroup *gbm_allocator_get_memory_group(GbmAllocator *allocator,
    const buffer_handle_t *buffer);
SuperMemoryGroup *gbm_allocator_get_free_group(GbmAllocator *allocator);
SuperMemoryGroup *super_memory_group_new(GbmAllocator *allocator, guint32 index);
SuperMemory *gbm_allocator_memory_new(GstMemoryFlags flags,
    GstAllocator *allocator, GstMemory *parent, gsize maxsize, gsize align,
    gsize offset, gsize size, gint plane, gpointer data,
    SuperMemoryGroup *group);
int gbm_allocator_allocate_buff(GbmAllocator *allocator, uint32_t width,
    uint32_t height, uint32_t format, uint64_t prod_usage_flags,
    uint64_t consum_usage_flags, uint32_t *stride, SuperMemory **mem,
    SuperMemoryGroup *group, uint32_t index);

G_END_DECLS

#endif /* __SUPER_GBM_ALLOCATOR_H__ */
