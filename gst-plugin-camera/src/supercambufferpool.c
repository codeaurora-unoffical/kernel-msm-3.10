#ifndef _GNU_SOURCE
#define _GNU_SOURCE            /* O_CLOEXEC */
#endif

#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <glib.h>
#include <time.h>

#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#include <gst/allocators/gstdmabuf.h>

#include "supercambufferpool.h"
#include "superstream.h"

#include "log/log.h"

#define MAX_INFLIGHT_REQUESTS (16)
#define NANOSECONDS_PER_SECOND (1000000000)

#define SUPER_BUFFER_POOL_IMPORT_QUARK super_buffer_pool_import_quark()

#define super_buffer_pool_parent_class parent_class

G_DEFINE_TYPE(SuperBufferPool, super_buffer_pool, GST_TYPE_BUFFER_POOL);


static void super_buffer_pool_release_buffer(GstBufferPool *bpool,
                                            GstBuffer *buffer);

static uint64_t super_get_ns_ts()
{
    uint64_t ts = 0;
    struct timespec time_var;
    clock_gettime(CLOCK_BOOTTIME, &time_var);

    ts = (((uint64_t) time_var.tv_sec) * NANOSECONDS_PER_SECOND) + ((uint64_t) time_var.tv_nsec);

    return ts;
}

static gboolean super_is_buffer_valid(GstBuffer *buffer,
                                     SuperMemoryGroup **out_group)
{
    gboolean valid = FALSE;
    GstMemory *mem = gst_buffer_peek_memory(buffer, 0);

    if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_TAG_MEMORY)) {
        return valid;
    }

    if (mem) {
        SuperMemory *vmem = (SuperMemory *) mem;
        SuperMemoryGroup *group = vmem->group;

        if (group->mem != gst_buffer_peek_memory(buffer, 0)) {
             return valid;
        }

        if (!gst_memory_is_writable(group->mem)) {
            return valid;
        }

        valid = TRUE;
        if (out_group) {
            *out_group = group;
        }
    }

    return valid;
}

static GQuark super_buffer_pool_import_quark()
{
    static GQuark quark = 0;

    if (quark == 0) {
        quark = g_quark_from_string("KmbBufferPoolUsePtrData");
    }

    return quark;
}

static void super_buffer_pool_reset_size(GbmAllocator *allocator,
                                        SuperMemoryGroup *group)
{
    gint i;
    gsize size;

    if (!allocator || !group) {
        GST_ERROR_OBJECT(allocator, "Invalid argument(s)! allocator at %p, "
                         "group at %p(at least one is NULL)", allocator, group);
        return;
    }

    size = ((SuperMemory *)group->mem)->size;
    gst_memory_resize(group->mem, 0, size);
}

static void super_cleanup_failed_alloc(GbmAllocator *allocator,
                                      SuperMemoryGroup *group)
{
    gint i;

    if (group->mems_allocated > 0) {
        gst_memory_unref(group->mem);
    } else {
        gst_atomic_queue_push(allocator->queue, group);
    }
}

static SuperMemory *super_buffer_pool_get_mmap_memory(SuperBufferPool *pool,
    SuperMemoryGroup *group, int i)
{
    GbmAllocator *allocator = pool->gbm_allocator;
    gpointer data = NULL;
    SuperMemory *mem = NULL;
    int ret;
    buffer_handle_t buff;
    guint stride;

    ret = gbm_allocator_allocate_buff(allocator, allocator->format.width,
        allocator->format.height, allocator->format.pix_fmt,
        GRALLOC1_PRODUCER_USAGE_CAMERA, pool->str->usage,
        &stride, &mem, group, i);
    if (ret) {
        super_cleanup_failed_alloc(allocator, group);
   }

    group->stream_buffer->buffer = &group->buffer;
    group->stream_buffer->stream = pool->output_stream;
    group->buf_idx = i;

    return mem;
}

static SuperMemoryGroup *super_buffer_pool_get_mem_group(SuperBufferPool *pool)
{
    GbmAllocator *allocator = pool->gbm_allocator;
    SuperMemoryGroup *group = gbm_allocator_get_free_group(allocator);
    gint i;

    if (!group) {
        return NULL;
    }

    if (group->mem == NULL) {
        group->mem = (GstMemory *) super_buffer_pool_get_mmap_memory(pool,
            group, pool->num_buffs);
        pool->num_buffs++;
    } else {
        gst_object_ref(allocator);
    }
    group->mems_allocated++;

    super_buffer_pool_reset_size(allocator, group);

    return group;
}

static GstFlowReturn super_buffer_pool_alloc_buffer(GstBufferPool *bpool,
    GstBuffer **buffer, GstBufferPoolAcquireParams *params)
{
    SuperBufferPool *pool = GST_SUPER_BUFFER_POOL(bpool);
    SuperMemoryGroup *group = NULL;
    GstBuffer *newbuf = NULL;
    GstVideoInfo *info = &pool->str->info;

    group = super_buffer_pool_get_mem_group(pool);
    if (group) {
        gint i;
        newbuf = gst_buffer_new();

        gst_buffer_append_memory(newbuf, group->mem);
    } else if (!newbuf) {
        GST_ERROR_OBJECT(pool, "failed to allocate buffer");
        return GST_FLOW_ERROR;
    }

    gst_buffer_add_video_meta_full(newbuf, GST_VIDEO_FRAME_FLAG_NONE,
                                   GST_VIDEO_INFO_FORMAT(info),
                                   GST_VIDEO_INFO_WIDTH(info),
                                   GST_VIDEO_INFO_HEIGHT(info),
                                   GST_VIDEO_INFO_N_PLANES(info),
                                   info->offset, info->stride);

    *buffer = newbuf;

    return GST_FLOW_OK;
}

camera3_stream_t *super_buffer_pool_get_stream(SuperBufferPool *pool)
{
    if (pool) {
        return pool->output_stream;
    }

    return NULL;
}

static gboolean super_buffer_pool_set_config(GstBufferPool *bpool,
    GstStructure *config)
{
    guint size, min_buffers, max_buffers;
    gboolean ret;
    GstAllocationParams params;
    GstAllocator *allocator;
    GstCaps *caps;
    SuperBufferPool *pool = GST_SUPER_BUFFER_POOL(bpool);
    SuperStream *str = pool->str;

    ret = gst_buffer_pool_config_get_params(config, &caps, &size, &min_buffers,
                                            &max_buffers);
    if (!ret) {
        GST_ERROR_OBJECT(pool, "invalid config %" GST_PTR_FORMAT, config);
        return FALSE;
    }

    ret = gst_buffer_pool_config_get_allocator(config, &allocator, &params);
    if (!ret) {
        GST_ERROR_OBJECT(pool, "invalid config %" GST_PTR_FORMAT, config);
        return FALSE;
    }

    GST_DEBUG_OBJECT(pool, "config %" GST_PTR_FORMAT, config);

    if (max_buffers > MAX_FRAME || max_buffers == 0) {

        GST_INFO_OBJECT(pool, "reducing maximum buffers to %u", max_buffers);
        max_buffers = MAX_FRAME;

        if (min_buffers > max_buffers) {
            min_buffers = max_buffers;
            GST_INFO_OBJECT(pool, "reducing minimum buffers"
                            " to %u", min_buffers);
        }
    }

    GST_INFO_OBJECT(pool, "adding needed video meta");
    gst_buffer_pool_config_add_option(config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    gst_buffer_pool_config_set_params(config, caps, str->info.size, min_buffers,
                                      max_buffers);

    gst_video_info_from_caps(&pool->caps_info, caps);

    ret = GST_BUFFER_POOL_CLASS(parent_class)->set_config(bpool, config);

    return ret;
}

static gboolean super_buffer_pool_start(GstBufferPool *bpool)
{
    gint ret;
    guint size, min_buffers, max_buffers, max_latency, count;
    gboolean res;
    GstStructure *config;
    GstCaps *caps;

    SuperBufferPool *pool = GST_SUPER_BUFFER_POOL(bpool);
    GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS(parent_class);

    GST_DEBUG_OBJECT(pool, "activating pool");

    config = gst_buffer_pool_get_config(bpool);
    res = gst_buffer_pool_config_get_params(config, &caps, &size, &min_buffers,
                                            &max_buffers);
    if (!res) {
        GST_ERROR_OBJECT(pool, "invalid config %" GST_PTR_FORMAT, config);
        gst_structure_free(config);
        return FALSE;
    }

    count = MAX_INFLIGHT_REQUESTS;

    gbm_allocator_start(pool->gbm_allocator, count);

    if (count != min_buffers) {
        GST_WARNING_OBJECT(pool, "Uncertain or not enough buffers. Minimum"
                           "buffers is now %d (was %d)", count, min_buffers);
        min_buffers = count;
    }

    max_latency = max_buffers;

    pool->size = size;
    pool->max_latency = max_latency;
    pool->num_queued = 0;

    if (max_buffers != 0 && max_buffers < min_buffers) {
        max_buffers = min_buffers;
    }

    gst_buffer_pool_config_set_params(config, caps, size, min_buffers,
                                      max_buffers);
    pclass->set_config(bpool, config);
    gst_structure_free(config);

    if (!pclass->start(bpool)) {
        GST_ERROR_OBJECT(pool, "failed to start streaming");
        return FALSE;
    }

    return TRUE;
}

static gboolean super_buffer_pool_stop(GstBufferPool *bpool)
{
    gint i, res;
    gboolean ret;

    SuperBufferPool *pool = GST_SUPER_BUFFER_POOL(bpool);
    GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS(parent_class);

    GST_DEBUG_OBJECT(pool, "Stopping buffer pool");

    if (pool->streaming) {
        GST_DEBUG_OBJECT(pool, "Stopped streaming");

        if (pool->gbm_allocator) {
            gbm_allocator_flush(pool->gbm_allocator);
        }

        pool->streaming = FALSE;

        for (i = 0; i < MAX_FRAME; i++) {
            if (pool->buffers[i]) {
                GstBuffer *buffer = pool->buffers[i];
                pool->buffers[i] = NULL;

                pclass->release_buffer(bpool, buffer);
                g_atomic_int_add(&pool->num_queued, -1);
            }
        }
        ret = GST_BUFFER_POOL_CLASS(parent_class)->stop(bpool);

        if (ret && pool->gbm_allocator) {
            AllocResult vret;
            vret = gbm_allocator_stop(pool->gbm_allocator);

            if (vret == ALLOC_BUSY) {
                GST_WARNING_OBJECT(pool, "Some buffers are still outstanding");
            }
            ret = (vret == ALLOC_OK);
        }
    }
    return ret;
}

static void super_buffer_pool_flush_start(GstBufferPool *bpool)
{
    SuperBufferPool *pool = GST_SUPER_BUFFER_POOL(bpool);

    GST_DEBUG_OBJECT(pool, "Start flushing");

    GST_OBJECT_LOCK(pool);
    pool->empty = FALSE;
    GST_OBJECT_UNLOCK(pool);
}

static void super_buffer_pool_flush_stop(GstBufferPool *bpool)
{
    gint i;
    GstBuffer *buffers[MAX_FRAME];
    gboolean res;
    SuperBufferPool *pool = GST_SUPER_BUFFER_POOL(bpool);

    GST_DEBUG_OBJECT(pool, "stop flushing");

    if (!pool->streaming) {
        GST_DEBUG_OBJECT(pool, "Starting streaming");

        pool->streaming = TRUE;
        return;
    }

    GST_OBJECT_LOCK(pool);

    GST_DEBUG_OBJECT(pool, "Stopped streaming");

    pool->streaming = FALSE;

    if (pool->gbm_allocator) {
        gbm_allocator_flush(pool->gbm_allocator);
    }

    for (i = 0; i < MAX_FRAME; i++) {
        buffers[i] = pool->buffers[i];
    }

    memset(pool->buffers, 0, sizeof(pool->buffers));

    GST_OBJECT_UNLOCK(pool);

    for (i = 0; i < MAX_FRAME; i++) {
        if (buffers[i]) {
            GstBufferPool *bpool = (GstBufferPool *) pool;
            GstBuffer *buffer = buffers[i];

            gst_mini_object_set_qdata(GST_MINI_OBJECT(buffer),
                                      SUPER_BUFFER_POOL_IMPORT_QUARK, NULL, NULL);

            if (!buffer->pool) {
                super_buffer_pool_release_buffer(bpool, buffer);
            }

            g_atomic_int_add(&pool->num_queued, -1);
        }
    }
}

static GstFlowReturn super_buffer_pool_acquire_buffer(GstBufferPool *bpool,
    GstBuffer **buffer, GstBufferPoolAcquireParams *params)
{
    gint i;
    GstFlowReturn res;
    SuperMemoryGroup *super_gr;
    gboolean last_buffer;
    camera3_stream_buffer_t *cam3_buffer;

    GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS(parent_class);
    SuperBufferPool *pool = GST_SUPER_BUFFER_POOL(bpool);

    GST_INFO_OBJECT(pool, "Acquire buffer");

    cam3_buffer = (camera3_stream_buffer_t *)
         g_async_queue_pop(pool->processed);

    super_gr = gbm_allocator_get_memory_group(pool->gbm_allocator,
                                            cam3_buffer->buffer);
    if (!super_gr) {
        return GST_FLOW_ERROR;
    }

    gst_memory_unref(super_gr->mem);

    *buffer = pool->buffers[super_gr->buf_idx];
    if (!*buffer) {
        GST_ERROR_OBJECT(pool, "No free buffer found for index %d.",
                         0);
        return GST_FLOW_ERROR;
    }

    last_buffer = g_atomic_int_dec_and_test(&pool->num_queued);
    if (last_buffer) {
        GST_OBJECT_LOCK(pool);
        pool->empty = TRUE;
        GST_OBJECT_UNLOCK(pool);
    }

    pool->buffers[super_gr->buf_idx] = NULL;

    pool->cur_fr_sent++;
    GST_BUFFER_OFFSET(*buffer) = pool->cur_fr_sent;
    GST_BUFFER_OFFSET_END(*buffer) = pool->cur_fr;

    GST_INFO_OBJECT(pool, "Acquired buffer %p", *buffer);
    return GST_FLOW_OK;
}

static GstFlowReturn supercam_buffer_pool_prepare_cr(SuperBufferPool *pool,
    GstBuffer *buf)
{
    gboolean res;

    SuperMemoryGroup *group = NULL;

    res = super_is_buffer_valid(buf, &group);
    if (!res) {
        GST_LOG_OBJECT(pool, "unref copied/invalid buffer %p", buf);
        gst_buffer_unref(buf);
        return GST_FLOW_OK;
    }

    if (pool->buffers[group->buf_idx] != NULL) {
        GST_ERROR_OBJECT(pool, "Buffer %i is already queued!", group->buf_idx);
        return GST_FLOW_ERROR;
    }

    GST_OBJECT_LOCK(pool);
    g_atomic_int_inc(&pool->num_queued);
    pool->buffers[group->buf_idx] = buf;

    gbm_allocator_prepare_buf(pool->gbm_allocator, group);

    pool->empty = FALSE;
    GST_OBJECT_UNLOCK(pool);

    g_async_queue_push(pool->pending, group->stream_buffer);

    return GST_FLOW_OK;
}

static void super_buffer_pool_release_buffer(GstBufferPool *bpool,
    GstBuffer *buffer)
{
    gboolean res;
    GstFlowReturn ret;
    SuperMemoryGroup *group;
    GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS(parent_class);
    SuperBufferPool *pool = GST_SUPER_BUFFER_POOL(bpool);

    GST_INFO_OBJECT(pool, "Release buffer %p", buffer);

    res = super_is_buffer_valid(buffer, &group);

    if (res) {
        ret = supercam_buffer_pool_prepare_cr(pool, buffer);
        if (ret != GST_FLOW_OK) {
            pclass->release_buffer(bpool, buffer);
        }
    } else {
        GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_TAG_MEMORY);
        pclass->release_buffer(bpool, buffer);
    }
}

static void super_buffer_pool_dispose(GObject *object)
{
    SuperBufferPool *pool = GST_SUPER_BUFFER_POOL(object);

    if (pool->gbm_allocator) {
        gst_object_unref(pool->gbm_allocator);
    }
    pool->gbm_allocator = NULL;

    if (pool->output_stream) {
        free(pool->output_stream);
        pool->output_stream = NULL;
    }

    if (pool->pending) {
        g_async_queue_unref(pool->pending);
        pool->pending = NULL;
    }

    if (pool->processed) {
        g_async_queue_unref(pool->processed);
        pool->processed = NULL;
    }

    if (pool->inflight) {
        g_async_queue_unref(pool->inflight);
        pool->inflight = NULL;
    }

    G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void super_buffer_pool_finalize(GObject *object)
{
    SuperBufferPool *pool = GST_SUPER_BUFFER_POOL(object);

    gst_object_unref(pool->str);

    if (pool->output_stream) {
        free(pool->output_stream);
        pool->output_stream = NULL;
    }

    if (pool->pending) {
        g_async_queue_unref(pool->pending);
        pool->pending = NULL;
    }

    if (pool->processed) {
        g_async_queue_unref(pool->processed);
        pool->processed = NULL;
    }

    if (pool->inflight) {
        g_async_queue_unref(pool->inflight);
        pool->inflight = NULL;
    }

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void super_buffer_pool_init(SuperBufferPool *pool)
{
    pool->empty = TRUE;
}

static void super_buffer_pool_class_init(SuperBufferPoolClass *class)
{
    GObjectClass *object_class = G_OBJECT_CLASS(class);
    GstBufferPoolClass *bufferpool_class = GST_BUFFER_POOL_CLASS(class);

    object_class->dispose = super_buffer_pool_dispose;
    object_class->finalize = super_buffer_pool_finalize;

    bufferpool_class->start = super_buffer_pool_start;
    bufferpool_class->stop = super_buffer_pool_stop;
    bufferpool_class->set_config = super_buffer_pool_set_config;
    bufferpool_class->alloc_buffer = super_buffer_pool_alloc_buffer;
    bufferpool_class->acquire_buffer = super_buffer_pool_acquire_buffer;
    bufferpool_class->release_buffer = super_buffer_pool_release_buffer;
    bufferpool_class->flush_start = super_buffer_pool_flush_start;
    bufferpool_class->flush_stop = super_buffer_pool_flush_stop;
}

GstBufferPool *super_buffer_pool_new(SuperStream *str, GstCaps *caps)
{
    gchar *parent_name, *name;
    GstStructure *config;
    SuperBufferPool *pool;

    /* setting a significant unique name */
    parent_name = gst_object_get_name(GST_OBJECT(str));
    name = g_strconcat(parent_name, ":", "pool:", "src", NULL);
    g_free(parent_name);

    pool = (SuperBufferPool *) g_object_new(GST_TYPE_SUPER_BUFFER_POOL, "name",
                                           name, NULL);
    g_free(name);

    pool->str = str;

    pool->gbm_allocator = gbm_allocator_new(GST_OBJECT(pool), &str->format);
    if (!pool->gbm_allocator) {
        GST_ERROR_OBJECT(pool, "Failed to create V4L2 allocator");
        gst_object_unref(pool);
        return NULL;
    }

    pool->output_stream = calloc(1, sizeof(*pool->output_stream));
    if (!pool->output_stream) {
        GST_ERROR_OBJECT(pool, "Cannot alloc output stream!");
        gst_object_unref(pool);
        return NULL;
    }

    pool->output_stream->stream_type = CAMERA3_STREAM_OUTPUT;
    pool->output_stream->width = str->format.width;
    pool->output_stream->height = str->format.height;
    pool->output_stream->format = str->format.pix_fmt;

    if (pool->output_stream->format == HAL_PIXEL_FORMAT_Y16) {
        pool->output_stream->data_space = HAL_DATASPACE_DEPTH;
    } else {
        pool->output_stream->data_space = HAL_DATASPACE_UNKNOWN;
    }
    pool->output_stream->usage = str->usage;
    pool->output_stream->rotation = 0;
    /* Fields to be filled by HAL (max_buffers, priv) are initialized to 0 */
    pool->output_stream->max_buffers = 0;
    pool->output_stream->priv = 0;

    pool->pending = g_async_queue_new();
    pool->processed = g_async_queue_new();
    pool->inflight = g_async_queue_new();

    gst_object_ref(str);

    config = gst_buffer_pool_get_config(GST_BUFFER_POOL_CAST(pool));
    gst_buffer_pool_config_set_params(config, caps, str->info.size, 0, 0);
    gst_buffer_pool_set_config(GST_BUFFER_POOL_CAST(pool), config);

    return GST_BUFFER_POOL(pool);
}
