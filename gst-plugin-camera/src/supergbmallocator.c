#include "config.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE            /* O_CLOEXEC */
#endif

#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <gbm_priv.h>

#include "supergbmallocator.h"

#define HAL_PIXEL_FORMAT_UBWCTP10      0x7FA30C09   ///< UBWCTP10
#define HAL_PIXEL_FORMAT_UBWCNV12      0x7FA30C06   ///< UBWCNV12
#define HAL_PIXEL_FORMAT_PD10          0x00000026   ///< PD10

#define gbm_allocator_parent_class parent_class
G_DEFINE_TYPE(GbmAllocator, gbm_allocator, GST_TYPE_ALLOCATOR);

static uint32_t gbm_allocator_get_gbm_format(uint32_t user_format)
{
    unsigned format;

    switch (user_format) {
      case HAL_PIXEL_FORMAT_BLOB:
          format = GBM_FORMAT_BLOB;
          GST_INFO("GBM_FORMAT_BLOB");
          break;
      case HAL_PIXEL_FORMAT_YCBCR_420_888:
          format = GBM_FORMAT_YCbCr_420_888;
          GST_INFO("GBM_FORMAT_YCbCr_420_888");
          break;
      case HAL_PIXEL_FORMAT_YCRCB_420_SP:
          format = GBM_FORMAT_YCbCr_420_SP;
          GST_INFO("GBM_FORMAT_YCbCr_420_888");
          break;
      case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
          /* TODO Until patches in CAMX are merged, then switch to
           * GBM_FORMAT_YCbCr_420_TP10_UBWC and fix image size calculations! */
          format = GBM_FORMAT_NV12_ENCODEABLE;
          GST_INFO("GBM_FORMAT_NV12_ENCODEABLE");
          break;
      case HAL_PIXEL_FORMAT_RAW10:
          format = GBM_FORMAT_RAW10;
          GST_INFO("GBM_FORMAT_RAW10");
          break;
      case HAL_PIXEL_FORMAT_RAW16:
          format = GBM_FORMAT_RAW16;
          GST_INFO("GBM_FORMAT_RAW16");
          break;
      case HAL_PIXEL_FORMAT_RAW_OPAQUE:
          format = GBM_FORMAT_RAW_OPAQUE;
          GST_INFO("GBM_FORMAT_RAW_OPAQUE");
          break;
      case HAL_PIXEL_FORMAT_UBWCTP10:
          format = GBM_FORMAT_YCbCr_420_TP10_UBWC;
          GST_INFO("GBM_FORMAT_YCbCr_420_TP10_UBWC");
          break;
      case HAL_PIXEL_FORMAT_UBWCNV12:
          format = GBM_FORMAT_YCbCr_420_SP_VENUS_UBWC;
          GST_INFO("GBM_FORMAT_YCbCr_420_SP_VENUS_UBWC");
          break;
      case HAL_PIXEL_FORMAT_PD10:
          format = GBM_FORMAT_P010;
          GST_INFO("GBM_FORMAT_P010");
          break;
      default:
          GST_ERROR("Format:0x%x not supported\n", user_format);
          format = -1;
    }

    return format;
}

static uint32_t gbm_allocator_get_usage_flag(uint32_t gbm_format,
    uint64_t cons_usage, uint64_t prod_usage)
{
    uint32_t gbm_usage;

    switch (gbm_format) {
        case GBM_FORMAT_IMPLEMENTATION_DEFINED:
        case GBM_FORMAT_NV12_ENCODEABLE:
            if ((cons_usage & GRALLOC1_CONSUMER_USAGE_CAMERA) &&
                (prod_usage & GRALLOC1_PRODUCER_USAGE_CAMERA)) {
                gbm_usage =
                    GBM_BO_USAGE_CAMERA_READ_QTI | GBM_BO_USAGE_CAMERA_WRITE_QTI;
            } else if (cons_usage & GRALLOC1_CONSUMER_USAGE_CAMERA) {
                gbm_usage = GBM_BO_USAGE_CAMERA_READ_QTI;
            }
            else if (prod_usage & GRALLOC1_PRODUCER_USAGE_CAMERA) {
                gbm_usage = GBM_BO_USAGE_CAMERA_WRITE_QTI;
            } else {
                gbm_usage =
                    GBM_BO_USAGE_CAMERA_READ_QTI | GBM_BO_USAGE_CAMERA_WRITE_QTI;
            }
            break;

        case GBM_FORMAT_YCbCr_420_TP10_UBWC:
        case GBM_FORMAT_YCbCr_420_SP_VENUS_UBWC:
            gbm_usage =  GBM_BO_USAGE_UBWC_ALIGNED_QTI;
            break;

        case GBM_FORMAT_YCbCr_420_P010_UBWC:
            break;

        case GBM_FORMAT_YCbCr_420_888:
        case GBM_FORMAT_YCbCr_420_SP:
             if ((cons_usage & GRALLOC1_CONSUMER_USAGE_CAMERA) &&
                 (prod_usage & GRALLOC1_PRODUCER_USAGE_CAMERA)) {
                 gbm_usage =
                     GBM_BO_USAGE_CAMERA_READ_QTI | GBM_BO_USAGE_CAMERA_WRITE_QTI;
            } else if (cons_usage & GRALLOC1_CONSUMER_USAGE_CAMERA) {
                gbm_usage = GBM_BO_USAGE_CAMERA_READ_QTI;
            } else if (prod_usage & GRALLOC1_PRODUCER_USAGE_CAMERA) {
                gbm_usage = GBM_BO_USAGE_CAMERA_WRITE_QTI;
            } else {
                gbm_usage =
                    GBM_BO_USAGE_CAMERA_READ_QTI | GBM_BO_USAGE_CAMERA_WRITE_QTI;
            }
            break;

        case GBM_FORMAT_RAW16:
        case GBM_FORMAT_RAW10:
             if ((cons_usage & GRALLOC1_CONSUMER_USAGE_CAMERA) &&
                 (prod_usage & GRALLOC1_PRODUCER_USAGE_CAMERA)) {
                 gbm_usage =
                     GBM_BO_USAGE_CAMERA_READ_QTI | GBM_BO_USAGE_CAMERA_WRITE_QTI;
            } else if (cons_usage & GRALLOC1_CONSUMER_USAGE_CAMERA) {
                gbm_usage = GBM_BO_USAGE_CAMERA_READ_QTI;
            } else if (prod_usage & GRALLOC1_PRODUCER_USAGE_CAMERA) {
                gbm_usage = GBM_BO_USAGE_CAMERA_WRITE_QTI;
            } else {
                gbm_usage =
                    GBM_BO_USAGE_CAMERA_READ_QTI | GBM_BO_USAGE_CAMERA_WRITE_QTI;
            }
           break;

        case GBM_FORMAT_BLOB:
             if ((cons_usage & GRALLOC1_CONSUMER_USAGE_CAMERA) &&
                 (prod_usage & GRALLOC1_PRODUCER_USAGE_CAMERA)) {
                 gbm_usage =
                     GBM_BO_USAGE_CAMERA_READ_QTI | GBM_BO_USAGE_CAMERA_WRITE_QTI;
            } else if (cons_usage & GRALLOC1_CONSUMER_USAGE_CAMERA) {
                gbm_usage = GBM_BO_USAGE_CAMERA_READ_QTI;
            } else if (prod_usage & GRALLOC1_PRODUCER_USAGE_CAMERA) {
                gbm_usage = GBM_BO_USAGE_CAMERA_WRITE_QTI;
            } else {
                gbm_usage =
                    GBM_BO_USAGE_CAMERA_READ_QTI | GBM_BO_USAGE_CAMERA_WRITE_QTI;
            }
            break;

       default:
        GST_ERROR("Unsupported format = %x", gbm_format);
        gbm_usage = -1;
    }

    return gbm_usage;
}

static struct gbm_bo* gbm_allocator_alloc_gbm_bo(GbmAllocator *allocator,
    uint32_t width, uint32_t height, uint32_t format,
    uint64_t producer_usage_flags, uint64_t consumer_usage_flags)
{
    uint32_t gbm_format = gbm_allocator_get_gbm_format(format);
    uint32_t gbm_usage =
        gbm_allocator_get_usage_flag(gbm_format, consumer_usage_flags,
                                     producer_usage_flags);
    struct gbm_bo *gbm_bo = NULL;

    if (gbm_format == -1 || gbm_usage == -1) {
        GST_ERROR_OBJECT(allocator, "Invalid format!");
        return NULL;
    }

    gbm_bo = gbm_bo_create(allocator->gbm_dev, width, height, gbm_format,
                           gbm_usage);
    if (!gbm_bo) {
        GST_ERROR("Failed to create GBM buffer object!");
        return NULL;
    }

    GST_INFO_OBJECT(allocator, "FD = %d Format: %x => width: %d height:%d"
        " stride:%d ", gbm_bo_get_fd(gbm_bo), gbm_bo_get_format(gbm_bo),
        gbm_bo_get_width(gbm_bo), gbm_bo_get_height(gbm_bo),
        gbm_bo_get_stride(gbm_bo));

    return gbm_bo;
}

static buffer_handle_t gbm_allocator_alloc_native_handle(struct gbm_bo *bo)
{
    native_handle_t *p_handle = NULL;
    if (bo) {
        p_handle = native_handle_create(1, 1);
        p_handle->data[0] = gbm_bo_get_fd(bo);
        /* NB! Here we pray that userspace addresses won't exceed a 32-bit int... */
        p_handle->data[1] = (int) bo;

        if(!p_handle) {
            GST_ERROR("Invalid native handle");
        }
    }

    return (buffer_handle_t) p_handle;
}

static void gbm_allocator_free_gbm_bo(struct gbm_bo *gbm_bo)
{
    if (gbm_bo) {
        gbm_bo_destroy(gbm_bo);
    }
}

int gbm_allocator_allocate_buff(GbmAllocator *allocator, uint32_t width,
    uint32_t height, uint32_t format, uint64_t prod_usage_flags,
    uint64_t consum_usage_flags, uint32_t *stride, SuperMemory **mem,
    SuperMemoryGroup *group, uint32_t index)
{
    int bo_fd, results = 0;
    size_t bo_size = 0;
    unsigned int ret;
    void *vaddr;
    struct gbm_bo *gbm_bo = NULL;

    if (!stride || !group || !mem) {
        GST_ERROR_OBJECT(allocator, "Invalid input!");
        return -EINVAL;
    }

    gbm_bo = gbm_allocator_alloc_gbm_bo(allocator, width, height, format,
        prod_usage_flags, consum_usage_flags);
    if (!gbm_bo) {
        GST_ERROR_OBJECT(allocator, "Invalid Gbm Buffer object");
        return -ENOMEM;
    }

    *stride = gbm_bo_get_stride(gbm_bo);
    if (*stride == 0) {
        GST_ERROR_OBJECT(allocator, "Invalid Stride length");
        results = -1;
        goto error;
    }

    ret = gbm_perform(GBM_PERFORM_GET_BO_SIZE, gbm_bo, &bo_size);
    if (ret != GBM_ERROR_NONE) {
        GST_ERROR_OBJECT(allocator, "Error in querying BO size");
        results = -1;
        goto error;
    }

    GST_DEBUG_OBJECT(allocator, "buffer object size: %lu", bo_size);

    group->buffer = gbm_allocator_alloc_native_handle(gbm_bo);
    if (!group->buffer) {
        GST_ERROR_OBJECT(allocator, "Error allocating native handle buffer");
        results = -1;
        goto error;
    }

    bo_fd = gbm_bo_get_fd(gbm_bo); /* DMA buf fd */
    if (bo_fd < 0) {
        GST_ERROR_OBJECT(allocator, "Cannot get GBM bo DMA fd!");
        results = -EBADFD;
        goto error;
    }

    vaddr = mmap(NULL, bo_size, PROT_READ | PROT_WRITE, MAP_SHARED, bo_fd, 0);
    if (vaddr == MAP_FAILED) {
        GST_ERROR_OBJECT(allocator, "Failed to mmap buffer: %s",
                         g_strerror(errno));
        results = -ENOMEM;
        goto error;
    }

    *mem = gbm_allocator_memory_new(0, GST_ALLOCATOR(allocator), NULL, bo_size,
                                    0, 0, bo_size, index, vaddr, group);
    (*mem)->buf_idx = index;

    return results;

error:
    if (gbm_bo) {
        gbm_allocator_free_gbm_bo(gbm_bo);
        gbm_bo = NULL;
    }

    return results;
}

static void gbm_allocator_release(GbmAllocator *allocator, SuperMemory *mem)
{
    SuperMemoryGroup *group = mem->group;

    if (g_atomic_int_dec_and_test(&group->mems_allocated)) {
        GST_LOG_OBJECT(allocator, "buffer %u released", group->buf_idx);
        gst_atomic_queue_push(allocator->queue, group);
    }

    g_object_unref(allocator);
}

static gpointer super_memory_map(SuperMemory *mem, gsize maxsize,
    GstMapFlags flags)
{
    return mem->data;
}

static gboolean super_memory_unmap(SuperMemory *mem)
{
    return TRUE;
}

static gboolean super_memory_dispose(SuperMemory *mem)
{
    gboolean ret;
    SuperMemoryGroup *group = mem->group;
    GbmAllocator *allocator = (GbmAllocator *) mem->mem.allocator;

    if (group->mem) {
        group->mem = gst_memory_ref((GstMemory *) mem);
        gbm_allocator_release(allocator, mem);
        ret = FALSE;
    } else {
        gst_object_ref(allocator);
        ret = TRUE;
    }

    return ret;
}

inline SuperMemory *gbm_allocator_memory_new(GstMemoryFlags flags,
    GstAllocator *allocator, GstMemory *parent, gsize maxsize, gsize align,
    gsize offset, gsize size, gint plane, gpointer data,
    SuperMemoryGroup * group)
{
    SuperMemory *mem = g_slice_new0(SuperMemory);
    gst_memory_init(GST_MEMORY_CAST(mem), flags, allocator, parent, maxsize,
                    align, offset, size);

    if (!parent) {
        mem->mem.mini_object.dispose =
            (GstMiniObjectDisposeFunction) super_memory_dispose;
    }

    mem->plane = plane;
    mem->data = data;
    mem->group = group;
    mem->size = size;

    return mem;
}

static void super_memory_group_free(SuperMemoryGroup *group)
{
    GstMemory *mem = group->mem;

    if (group->stream_buffer) {
        free(group->stream_buffer);
        group->stream_buffer = NULL;
    }

    group->mem = NULL;

    g_slice_free(SuperMemoryGroup, group);
}

SuperMemoryGroup *super_memory_group_new(GbmAllocator *allocator, guint32 index)
{
    SuperMemoryGroup *group = g_slice_new(SuperMemoryGroup);
    memset(group, 0, sizeof(*group));

    group->stream_buffer = calloc(1, sizeof(*group->stream_buffer));
    if (!group->stream_buffer) {
        GST_ERROR_OBJECT(allocator, "Cannot allocate stream buffer handle!");
        goto failed;
    }

    /* Very important to set fences to -1, otherwise CamX will timeout on
     * process_capture_request */
    group->stream_buffer->acquire_fence = -1;
    group->stream_buffer->release_fence = -1;

    return group;

failed:
    super_memory_group_free(group);
    return NULL;
}

static void gbm_allocator_free(GstAllocator *gallocator, GstMemory *gmem)
{
    GbmAllocator *allocator = (GbmAllocator *) gallocator;
    SuperMemory *mem = (SuperMemory *) gmem;
    SuperMemoryGroup *group = mem->group;
    native_handle_t *n_handle;

    if (!mem->mem.parent) {
        if (mem->data) {
            munmap(mem->data, mem->size);
        }

        n_handle = (native_handle_t *) group->buffer;

        if (n_handle->data[0]) {
            close(n_handle->data[0]);
        }

        if (n_handle->data[1]) {
            gbm_allocator_free_gbm_bo((struct gbm_bo *) n_handle->data[1]);
        }

        native_handle_close(group->buffer);
        native_handle_delete(n_handle);
    }

    g_slice_free(SuperMemory, mem);
}

static void gbm_allocator_dispose(GObject *obj)
{
    gint i;
    SuperMemoryGroup *group;
    GbmAllocator *allocator = (GbmAllocator *) obj;

    for (i = 0; i < allocator->count; i++) {
        group = ((GbmAllocator *)obj)->groups[i];
        if (group) {
            super_memory_group_free(group);
        }

        allocator->groups[i] = NULL;
    }

    if (allocator->gbm_dev) {
        gbm_device_destroy(allocator->gbm_dev);
    }

    if (allocator->gbm_dev_fd >= 0) {
        close(allocator->gbm_dev_fd);
    }

    GST_LOG_OBJECT(obj, "called");

    G_OBJECT_CLASS(parent_class)->dispose(obj);
}

static void gbm_allocator_finalize(GObject *obj)
{
    GbmAllocator *allocator = (GbmAllocator *) obj;

    GST_LOG_OBJECT(obj, "called");

    gst_atomic_queue_unref(allocator->queue);

    if (allocator->gbm_dev) {
        gbm_device_destroy(allocator->gbm_dev);
    }

    if (allocator->gbm_dev_fd >= 0) {
        close(allocator->gbm_dev_fd);
    }

    G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void gbm_allocator_class_init(GbmAllocatorClass *alloc_class)
{
    GstAllocatorClass *alloc =  (GstAllocatorClass *) alloc_class;
    GObjectClass *obj = (GObjectClass *) alloc_class;

    obj->finalize = gbm_allocator_finalize;
    obj->dispose = gbm_allocator_dispose;

    alloc->alloc = NULL;
    alloc->free = gbm_allocator_free;
}

static void gbm_allocator_init(GbmAllocator *gbm_allocator)
{
    GstAllocator *gst_alloc = GST_ALLOCATOR_CAST(gbm_allocator);

    gst_alloc->mem_unmap   = (GstMemoryUnmapFunction) super_memory_unmap;
    gst_alloc->mem_map     = (GstMemoryMapFunction) super_memory_map;

    GST_OBJECT_FLAG_SET(gbm_allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);

    gbm_allocator->queue = gst_atomic_queue_new(MAX_FRAME);
}

GbmAllocator *gbm_allocator_new(GstObject *parent, SuperHALFormat *format)
{
    gchar *name, *parent_name;
    GbmAllocator *allocator;

    parent_name = gst_object_get_name(parent);
    name = g_strconcat(parent_name, ":GBM-allocator", NULL);
    g_free(parent_name);

    allocator = g_object_new(GST_TYPE_GBM_ALLOCATOR, "name", name, NULL);
    g_free(name);

    allocator->format = *format;

    allocator->gbm_dev_fd = open(GBM_DEVICE, O_RDWR);
    if (allocator->gbm_dev_fd < 0) {
        GST_ERROR_OBJECT(allocator, "Cannot open GBM device: %s",
                         strerror(errno));
        gst_object_unref(allocator);
        allocator = NULL;
        goto exit;
    }

    allocator->gbm_dev = gbm_create_device(allocator->gbm_dev_fd);
    if (!allocator->gbm_dev) {
        GST_ERROR_OBJECT(allocator, "Cannot create GBM device!");
        gst_object_unref(allocator);
        allocator = NULL;
        goto exit;
    }

exit:
    return allocator;
}

void gbm_allocator_start(GbmAllocator *allocator, guint32 num_bufs)
{
    gint i;

    GST_OBJECT_LOCK(allocator);

    if (g_atomic_int_get(&allocator->active)) {
        GST_ERROR_OBJECT(allocator, "allocator already active");
        GST_OBJECT_UNLOCK(allocator);
        return;
    }

    allocator->count = num_bufs;

    for (i = 0; i < allocator->count; i++) {
        allocator->groups[i] = super_memory_group_new(allocator, i);
        if (allocator->groups[i] == NULL) {
            GST_OBJECT_UNLOCK(allocator);
            return;
        }
        gst_atomic_queue_push(allocator->queue, allocator->groups[i]);
    }

    g_atomic_int_set(&allocator->active, TRUE);
    GST_OBJECT_UNLOCK(allocator);
}

AllocResult gbm_allocator_stop(GbmAllocator *allocator)
{
    guint len;
    gint i = 0;
    AllocResult ret = ALLOC_OK;

    GST_DEBUG_OBJECT(allocator, "stop allocator");

    GST_OBJECT_LOCK(allocator);

    if (!g_atomic_int_get(&allocator->active)) {
        goto done;
    }

    len = gst_atomic_queue_length(allocator->queue);
    if (len != allocator->count) {
        GST_DEBUG_OBJECT(allocator, "allocator is still in use");
        ret = ALLOC_BUSY;
        goto done;
    }

    while (gst_atomic_queue_pop(allocator->queue)) {
        /* Pop everything */
    };

    for (i = 0; i < allocator->count; i++) {
        if (allocator->groups[i]) {
            super_memory_group_free(allocator->groups[i]);
        }
        allocator->groups[i] = NULL;
    }
    allocator->count = 0;

    g_atomic_int_set(&allocator->active, FALSE);

done:
    GST_OBJECT_UNLOCK(allocator);
    return ret;
}

SuperMemoryGroup *gbm_allocator_get_free_group(GbmAllocator *allocator)
{
    SuperMemoryGroup *group;

    if (!g_atomic_int_get(&allocator->active)) {
        return NULL;
    }

    group = gst_atomic_queue_pop(allocator->queue);

    return group;
}

void gbm_allocator_flush(GbmAllocator *allocator)
{
    gint i;

    GST_OBJECT_LOCK(allocator);

    if (!g_atomic_int_get(&allocator->active)) {
        GST_OBJECT_UNLOCK(allocator);
        return;
    }

    for (i = 0; i < allocator->count; i++) {
        SuperMemoryGroup *group = allocator->groups[i];

        gst_memory_unref(group->mem);
    }

    GST_OBJECT_UNLOCK(allocator);
}

void gbm_allocator_prepare_buf(GbmAllocator *allocator, SuperMemoryGroup *group)
{
    gint i;

    if (!g_atomic_int_get(&allocator->active)) {
        return;
    }

    gst_memory_ref(group->mem);
}

SuperMemoryGroup *gbm_allocator_get_memory_group(GbmAllocator *allocator,
                                                const buffer_handle_t *buffer)
{
    SuperMemoryGroup *group = NULL;
    gint i;

    g_return_val_if_fail(g_atomic_int_get(&allocator->active), NULL);
    g_return_val_if_fail(buffer != NULL, NULL);


    for (i = 0; i < allocator->count; ++i) {
        if (allocator->groups[i]->buffer == *buffer) {
            group = allocator->groups[i];
        }
    }

    return group;
}
