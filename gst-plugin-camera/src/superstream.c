#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>

#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include <hardware/hardware.h>
#include <system/camera_metadata.h>
//#include <hardware/gralloc1.h>
#include "superstream.h"

#define FIXATE_WIDTH (1280)
#define FIXATE_HEIGHT (720)
#define MIN_BUFFERS (2)

#define SUPER_STREAM_FORMAT_COUNT (G_N_ELEMENTS(super_formats))

#define superstream_parent_class parent_class

G_DEFINE_TYPE(SuperStream, superstream, GST_TYPE_PUSH_SRC);

static const camera_metadata_enum_android_scaler_available_formats_t
    super_formats[] = {
    ANDROID_SCALER_AVAILABLE_FORMATS_YV12,
    ANDROID_SCALER_AVAILABLE_FORMATS_YCrCb_420_SP,
    ANDROID_SCALER_AVAILABLE_FORMATS_YCbCr_420_888,
    ANDROID_SCALER_AVAILABLE_FORMATS_IMPLEMENTATION_DEFINED
};

static void superstream_init(SuperStream *stream)
{
    stream->formats = NULL;

    gst_base_src_set_format(GST_BASE_SRC(stream), GST_FORMAT_TIME);
    gst_base_src_set_live(GST_BASE_SRC(stream), TRUE);
}

GstElement *super_stream_create(const gpointer cam_bin, const gchar *name,
    int (*notify_bp_created)(gpointer cam_bin), int cam_id)
{
    GstElement *stream = NULL;

    g_return_val_if_fail(cam_bin != NULL, NULL);
    g_return_val_if_fail(name != NULL, NULL);
    g_return_val_if_fail(notify_bp_created != NULL, NULL);

    stream = g_object_new(GST_TYPE_SUPERSTREAM, "name", name, NULL);
    if (!stream) {
        return NULL;
    }

    GST_SUPERSTREAM(stream)->cam_bin = cam_bin;
    GST_SUPERSTREAM(stream)->bp_created_cb = notify_bp_created;
    GST_SUPERSTREAM(stream)->cam_id = cam_id;

    return stream;
}

static void super_stream_clear_formats_list(SuperStream *stream)
{
    g_list_free(stream->formats);
    stream->formats = NULL;
}

static void super_stream_finalize(SuperStream *stream)
{
    if (stream->formats) {
        super_stream_clear_formats_list(stream);
    }

    if (stream->probed_caps) {
        gst_caps_unref(stream->probed_caps);
    }

    G_OBJECT_CLASS(parent_class)->finalize((GObject *) (stream));
}

static gint compare_by_frame_size(GstStructure *lhs, GstStructure *rhs)
{
    int lhs_height, lhs_width, rhs_height, rhs_width;

    gst_structure_get_int(lhs, "height", &lhs_height);
    gst_structure_get_int(lhs, "width", &lhs_width);
    gst_structure_get_int(rhs, "height", &rhs_height);
    gst_structure_get_int(rhs, "width", &rhs_width);

    return ((rhs_height * rhs_width) - (lhs_height * lhs_width));
}

gboolean super_stream_save_formats(SuperStream *str, GList *formats)
{
    g_return_val_if_fail(str != NULL, FALSE);
    g_return_val_if_fail(formats != NULL, FALSE);

    /* shallow copy, no ptrs in format struct */
    str->formats = g_list_copy(formats);

    return TRUE;
}

static GstVideoFormat super_stream_halpixelformat_to_video_format(
    guint32 hal_format)
{
    GstVideoFormat gst_format;

    switch (hal_format) {
    case ANDROID_SCALER_AVAILABLE_FORMATS_IMPLEMENTATION_DEFINED:
        gst_format = GST_VIDEO_FORMAT_NV12;
        break;
    case ANDROID_SCALER_AVAILABLE_FORMATS_YCbCr_420_888:
        /* temporary */
        gst_format = GST_VIDEO_FORMAT_NV12;
        break;
    default:
        gst_format = GST_VIDEO_FORMAT_UNKNOWN;
        break;
    }

    return gst_format;
}

static GstStructure *super_stream_halpixelformat_to_gst_struct(guint32 format)
{
    GstStructure *structure = NULL;

    switch (format) {
    case ANDROID_SCALER_AVAILABLE_FORMATS_IMPLEMENTATION_DEFINED:
    case ANDROID_SCALER_AVAILABLE_FORMATS_YCbCr_420_888: {
        GstVideoFormat gst_format;
        gst_format = super_stream_halpixelformat_to_video_format(format);
        if (gst_format != GST_VIDEO_FORMAT_UNKNOWN) {
            structure = gst_structure_new("video/x-raw", "format",
                    G_TYPE_STRING, gst_video_format_to_string(gst_format), NULL);
        }
        break;
    }
    default:
        GST_DEBUG("Unsupported fourcc 0x%08x %" GST_FOURCC_FORMAT, format,
                  GST_FOURCC_ARGS(format));
        break;
    }

    return structure;
}

static GstCaps *super_stream_get_caps_from_formats(SuperStream *stream)
{
    int res;
    GstCaps *ret = gst_caps_new_empty();
    GList *results = NULL;
    GList *cur_fmt;

    for (cur_fmt = stream->formats; cur_fmt; cur_fmt = cur_fmt->next) {
        SuperHALFormat *format = (SuperHALFormat *) cur_fmt->data;
        GstStructure *template =
            super_stream_halpixelformat_to_gst_struct(format->pix_fmt);

        if (template) {
            gst_structure_set(template, "width", G_TYPE_INT,
                (gint) format->width, "height", G_TYPE_INT,
                (gint) format->height, NULL);

            results = g_list_prepend(results, template);
        }

    }
    GST_DEBUG_OBJECT(stream, "Done iterating frame sizes");

    results = g_list_reverse (results);
    while (results != NULL) {
        gst_caps_append_structure(ret, results->data);
        results = g_list_delete_link(results, results); // TODO should free all temp inserted in it - please check!
    }

    return ret;
}

static void super_stream_set_all_features(GstCaps *caps)
{
    GstCapsFeatures *feat_any;
    GstCapsFeatures *feat_copy;
    guint size;
    guint i;

    g_return_if_fail(caps != NULL);

    feat_any = gst_caps_features_new_any();
    g_return_if_fail(feat_any != NULL);

    /* TODO there's a bug here, causing segfaults - fix later!!!
     * this is the sort of caps with featerus i.e 'video-x/raw(memory:DMABuf)
     * */
    size = gst_caps_get_size(caps);
    if (!size) {
        return;
    }
    // if size is 0 we will enter loop and probably segfault!!!
    for (i = 0; i < size - 1; i++) {
        feat_copy = gst_caps_features_copy(feat_any);
        gst_caps_set_features(caps, i, feat_copy);
    }
    gst_caps_set_features(caps, i, feat_any);
}

static GstCaps *super_stream_probe_caps(SuperStream *stream, GstCaps *filter)
{
    GstCaps *ret;
    GList *cur_fmt;

    ret = gst_caps_new_empty();

    GstCaps *tmp = super_stream_get_caps_from_formats(stream);

    if (gst_caps_is_empty(tmp)) {
        GST_ERROR_OBJECT(stream, "Cannot get caps from all formats!");
        return NULL;
    }

    if (tmp) {
        gst_caps_append(ret, tmp);
    }

    super_stream_set_all_features(ret);

    if (filter) {
        GstCaps *tmp = ret;
        ret = gst_caps_intersect_full(filter, ret, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(tmp);
    }

    return ret;
}

static GstCaps *super_stream_get_caps(SuperStream *stream, GstCaps *filter)
{
    GstCaps *ret;

    if (!stream->probed_caps) {
        stream->probed_caps = super_stream_probe_caps(stream, NULL);
    }

    if (filter) {
        ret = gst_caps_intersect_full(filter, stream->probed_caps,
                                      GST_CAPS_INTERSECT_FIRST);
    } else {
        ret = gst_caps_ref(stream->probed_caps);
    }

    GST_INFO_OBJECT(stream, "Probed caps: %" GST_PTR_FORMAT, ret);

    return ret;
}

static GstCaps *super_stream_get_pad_caps(GstBaseSrc *src, GstCaps *filter)
{
    SuperStream *stream = GST_SUPERSTREAM(src);

    if (!stream->formats) {
        return gst_pad_get_pad_template_caps(GST_BASE_SRC_PAD(stream));
    }

    return super_stream_get_caps(stream, filter);
}

static gboolean super_stream_format_is_yuv(guint32 pix_fmt)
{
    return TRUE;
}

static guint32 super_stream_get_plane_size(const SuperHALFormat *fmt,
    const guint plane_idx)
{
    guint32 res = 0;

    if (plane_idx < 0 || plane_idx > MAX_PLANES) {
        GST_ERROR("Invalid plane idx!");
        return res;
    }

    if (super_stream_format_is_yuv(fmt->pix_fmt)) {
        /* To be replaced with
         * ANDROID_SCALER_AVAILABLE_FORMATS_IMPLEMENTATION_DEFINED when
         * GST_VIDEO_FORMAT_NV12 is mapped to it */
        if (fmt->pix_fmt == ANDROID_SCALER_AVAILABLE_FORMATS_YCbCr_420_888) {

            res = GST_ROUND_UP_N(fmt->stride[plane_idx], 64) *
                  GST_ROUND_UP_N((fmt->height / (1 + plane_idx)), 64);
        }
    } else {
        GST_WARNING("Unsupported format!");
    }

    return res;
}

static void super_stream_calc_sizeimage(SuperHALFormat *format)
{
    guint plane_size, i;

    if (!format) {
        GST_ERROR("Invalid input!");
        return;
    }

    for (i = 0; i < format->planes_cnt; ++i) {
        plane_size = super_stream_get_plane_size(format, i);
        format->total_size += plane_size;
        format->plane_size[i] = plane_size;
    }
}

static void super_stream_save_format(SuperStream *stream, SuperHALFormat *format,
    GstVideoInfo *info, GstVideoAlignment *align)
{
    const GstVideoFormatInfo *finfo = info->finfo;
    gint stride, padded_width, padded_height, i;

    info->width = format->width;
    info->height = format->height;

    stride = format->stride[0];

    padded_width = stride / GST_VIDEO_FORMAT_INFO_PSTRIDE(finfo, 0);

    align->padding_right = padded_width - info->width - align->padding_left;

    padded_height = format->height;

    align->padding_bottom = padded_height - info->height - align->padding_top;

    stream->planes_cnt = MAX(1, format->planes_cnt);
    info->size = 0;

    super_stream_calc_sizeimage(format);
    for (i = 0; i < format->planes_cnt; i++) {
        stride = format->stride[i];

        info->stride[i] = stride;
        info->offset[i] = info->size;
        info->size += format->plane_size[i];
    }

    for (i = 0; i < finfo->n_planes; i++) {
        gint horiz_edge, vert_edge;

        horiz_edge = GST_VIDEO_FORMAT_INFO_SCALE_WIDTH(finfo, i,
                align->padding_left);
        vert_edge = GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT(finfo, i,
                align->padding_top);

        info->offset[i] += (vert_edge * info->stride[i]) +
                (horiz_edge * GST_VIDEO_INFO_COMP_PSTRIDE(info, i));
    }

    stream->format = *format;
    stream->info = *info;
    stream->align = *align;
}

static gboolean super_stream_setup_pool(SuperStream *stream, GstCaps *caps)
{
    GST_DEBUG_OBJECT(stream, "Initializing the capture system");

    if (!stream->min_buffers) {
        stream->min_buffers = MIN_BUFFERS;
    }

    GST_LOG_OBJECT(stream, "Initializing buffer pool");

    stream->pool = (SuperBufferPool *) super_buffer_pool_new(stream, caps);
    if (!stream->pool) {
        GST_ELEMENT_ERROR(stream, RESOURCE, READ,
                          (("Could not map buffers from device")),
                          ("Failed to create buffer pool: %s",
                          g_strerror(errno)));
        return FALSE;
    }

    if (stream->bp_created_cb) {
        return stream->bp_created_cb(stream->cam_bin);
    }

    return TRUE;
}

static SuperHALFormat *super_stream_get_format_from_fourcc_and_size(
    SuperStream *str, guint32 fourcc, gint w, gint h)
{
    SuperHALFormat *fmt;
    GList *cur_format;

    if (fourcc == 0) {
        return NULL;
    }

    cur_format = str->formats;

    while (cur_format) {
        fmt = (SuperHALFormat *) cur_format->data;
        if (fmt->pix_fmt == fourcc && fmt->width == w && fmt->height == h) {
            return fmt;
        }

        cur_format = g_list_next(cur_format);
    }

    return NULL;
}

static gboolean super_stream_get_caps_info(SuperStream *str, GstCaps *caps,
    SuperHALFormat **format, GstVideoInfo *info)
{
    const gchar *mimetype;
    GstStructure *structure;
    guint32 fourcc = 0;
    SuperHALFormat *fmt = NULL;

    structure = gst_caps_get_structure(caps, 0);

    mimetype = gst_structure_get_name(structure);

    if (!gst_video_info_from_caps(info, caps)) {
        GST_DEBUG_OBJECT(str, "invalid format");
        return FALSE;
    }

    if (g_str_equal(mimetype, "video/x-raw")) {
        switch (GST_VIDEO_INFO_FORMAT(info)) {
        case GST_VIDEO_FORMAT_NV12:
            /* HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED; */
            fourcc = HAL_PIXEL_FORMAT_YCBCR_420_888;
            break;
        case GST_VIDEO_FORMAT_I420:
            fourcc = HAL_PIXEL_FORMAT_YCBCR_420_888;
            break;
        default:
            break;
        }
    }

    fmt = super_stream_get_format_from_fourcc_and_size(str, fourcc, info->width,
        info->height);

    if (fmt == NULL) {
        GST_DEBUG_OBJECT(str, "unsupported format");
        return FALSE;
    }

    *format = fmt;

    return TRUE;
}

static gboolean super_stream_set_format(SuperStream *str, GstCaps *caps)
{
    int res;
    gboolean ret;
    gint stride, i = 0;
    GstVideoInfo info;
    GstVideoAlignment align; // TODO ???
    SuperHALFormat *fmt;

    memset(&fmt, 0, sizeof(fmt));

    gst_video_info_init(&info);
    gst_video_alignment_reset(&align);

    ret = super_stream_get_caps_info(str, caps, &fmt, &info);
    if (ret != TRUE) {
        GST_DEBUG_OBJECT(str, "can't parse caps %" GST_PTR_FORMAT,
                         caps);
        return FALSE;
    }

    fmt->planes_cnt = GST_VIDEO_INFO_N_PLANES(&info);
    if (!fmt->planes_cnt) {
        fmt->planes_cnt = 1;
    }

    for (i = 0; i < fmt->planes_cnt; i++) {
        stride = GST_VIDEO_INFO_PLANE_STRIDE(&info, i);

        fmt->stride[i] = stride;
    }

    GST_DEBUG_OBJECT(str, "Desired format is %dx%d, nb planes %d", fmt->width,
                     fmt->height, fmt->planes_cnt);

    for (i = 0; i < fmt->planes_cnt; i++) {
        GST_DEBUG_OBJECT(str, "  stride %u", fmt->stride[i]);
    }

    super_stream_save_format(str, fmt, &info, &align);

    if (!super_stream_setup_pool(str, caps)) {
        return FALSE;
    }

    return TRUE;
}

static GstCaps *super_stream_fixate(GstBaseSrc *basesrc, GstCaps *caps)
{
    GstStructure *structure;
    gint i;

    caps = gst_caps_make_writable(caps);

    for (i = 0; i < gst_caps_get_size(caps); ++i) {
        structure = gst_caps_get_structure(caps, i);

        if (gst_structure_has_field(structure, "format")) {
            gst_structure_fixate_field(structure, "format");
        }

        if (gst_structure_has_field(structure, "height")) {
            gst_structure_fixate_field_nearest_int(structure, "height",
                                                   FIXATE_HEIGHT);
        }

        if (gst_structure_has_field(structure, "width")) {
            gst_structure_fixate_field_nearest_int(structure, "width",
                                                   FIXATE_WIDTH);
        }
    }

    caps = GST_BASE_SRC_CLASS(parent_class)->fixate(basesrc, caps);

    return caps;
}

static gboolean super_stream_are_caps_equal(SuperStream *stream, GstCaps *caps)
{
    gboolean ret;

    GstStructure *config;
    GstCaps *oldcaps;

    if (!stream->pool) {
        return FALSE;
    }

    config = gst_buffer_pool_get_config((GstBufferPool *)stream->pool);
    gst_buffer_pool_config_get_params(config, &oldcaps, NULL, NULL, NULL);

    ret = oldcaps && gst_caps_is_equal(caps, oldcaps);

    gst_structure_free(config);

    return ret;
}

static gboolean super_stream_stop_pool(SuperStream *stream)
{
    gboolean ret = TRUE;
    GST_DEBUG_OBJECT(stream, "stopping");

    if (stream->pool) {
        GST_DEBUG_OBJECT(stream, "deactivating pool");
        gst_buffer_pool_set_active((GstBufferPool *) stream->pool, FALSE); // ->buffer_pool_stop()
        gst_object_unref(stream->pool);
        stream->pool = NULL;
    }

    return ret;
}

static gboolean super_stream_set_caps(GstBaseSrc *src, GstCaps *caps)
{
    gboolean res;

    SuperStream *stream = GST_SUPERSTREAM(src);

    if (super_stream_are_caps_equal(stream, caps)) {
        return TRUE;
    }

    if (!super_stream_stop_pool(stream)) {
        return FALSE;
    }

    return super_stream_set_format(stream, caps);
}

static gboolean super_stream_negotiate(GstBaseSrc *basesrc)
{
    GstCaps *caps = NULL;
    GstCaps *source_caps;
    GstCaps *sink_caps = NULL;
    gboolean result = FALSE;

    source_caps = gst_pad_query_caps(GST_BASE_SRC_PAD(basesrc), NULL);
    GST_INFO_OBJECT(basesrc, "Negotiate of %s, caps: %" GST_PTR_FORMAT,
                    basesrc->element.object.name, source_caps);

    if (!source_caps || gst_caps_is_any(source_caps)) {
        GST_DEBUG_OBJECT(basesrc, "no negotiation needed");
        if (source_caps) {
            gst_caps_unref(source_caps);
        }
        return TRUE;
    }

    sink_caps = gst_pad_peer_query_caps(GST_BASE_SRC_PAD(basesrc), NULL);

    GST_INFO_OBJECT(basesrc, "caps of sink: %" GST_PTR_FORMAT, sink_caps);
    if (sink_caps && !gst_caps_is_any(sink_caps)) {
        GstCaps *icaps = NULL;

        icaps = gst_caps_intersect_full(sink_caps, source_caps,
                                        GST_CAPS_INTERSECT_FIRST);
        caps = gst_caps_copy_nth(icaps, 0);

        gst_caps_unref(icaps);
    } else {
        caps = source_caps;
    }
    if (sink_caps) {
        gst_caps_unref(sink_caps);
    }
    if (caps) {
        caps = gst_caps_truncate(caps);

        if (!gst_caps_is_empty(caps)) {
            caps = super_stream_fixate(basesrc, caps);
            GST_DEBUG_OBJECT(basesrc, "fixated to: %" GST_PTR_FORMAT, caps);

            if (gst_caps_is_any(caps)) {
                result = TRUE;
            } else if (gst_caps_is_fixed(caps)) {
                result = gst_base_src_set_caps(basesrc, caps);
            }
        }
        gst_caps_unref(caps);
    }
    return result;
}

static gboolean super_stream_decide_allocation_helper(SuperStream *stream,
    GstQuery *query)
{
    int res;
    GstCaps *caps;
    GstStructure *config;
    guint size = 0, min = 0, max = 0, own_min = 0;
    GstAllocator *alloc = NULL;
    GstBufferPool *bp = NULL;
    GstAllocationParams allocation_prm = { 0 };

    gst_query_parse_allocation(query, &caps, NULL);

    if (!stream->pool) {
        res = super_stream_setup_pool(stream, caps);
        if (res != TRUE) {
            goto cleanup;
        }
    }

    if (gst_query_get_n_allocation_pools(query) > 0) {
        GstBufferPool *downstream_pool = NULL;
        guint tmp_size = 0, tmp_min = 0, tmp_max = 0;

        gst_query_parse_nth_allocation_pool(query, 0, &downstream_pool,
            &tmp_size, &tmp_min, &tmp_max);
        if (downstream_pool) {
            GST_INFO_OBJECT(stream, "Replacing pool from downstream"
                            " element with our own!");

            gst_query_set_nth_allocation_pool(query, 0,
                GST_BUFFER_POOL_CAST(stream->pool), stream->info.size, 1, 0);
        }
    }

    stream->min_buffers = MIN_BUFFERS;

    bp = gst_object_ref(stream->pool);
    size = stream->info.size;
    GST_DEBUG_OBJECT(stream, "Streaming mode: using our own pool"
                     " %" GST_PTR_FORMAT, bp);

    if (size == 0) {
        GST_ELEMENT_ERROR(stream, RESOURCE, SETTINGS,
                (("Video device did not suggest any buffer size.")), (NULL));
        goto cleanup;
    }

    own_min = min + stream->min_buffers + 1;

    config = gst_buffer_pool_get_config((GstBufferPool *)stream->pool);

    gst_buffer_pool_config_add_option(config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    gst_buffer_pool_config_set_allocator(config, alloc, &allocation_prm);
    gst_buffer_pool_config_set_params(config, caps, size, own_min, 0);

    GST_DEBUG_OBJECT(stream, "Setting own pool config to %"
                     GST_PTR_FORMAT, config);

    if (!gst_buffer_pool_set_config((GstBufferPool *)stream->pool, config)) {
        config = gst_buffer_pool_get_config((GstBufferPool *)stream->pool);

        GST_DEBUG_OBJECT(stream, "Own pool config changed to %"
                         GST_PTR_FORMAT, config);

        if (!gst_buffer_pool_set_config((GstBufferPool *)stream->pool, config)) {
            GST_ELEMENT_ERROR(stream, RESOURCE, SETTINGS,
                    (("Failed to configure internal buffer pool.")), (NULL));
            goto cleanup;
        }
    }

    if (bp) {
        config = gst_buffer_pool_get_config(bp);
        gst_buffer_pool_config_get_params(config, NULL, &size, &min, &max);
        gst_structure_free(config);
    }

    gst_query_add_allocation_pool(query, bp, size, min, max);

    if (alloc) {
        gst_object_unref(alloc);
    }

    if (bp) {
        gst_object_unref(bp);
    }

    return TRUE;

cleanup:

    if (alloc) {
        gst_object_unref(alloc);
    }

    if (bp) {
        gst_object_unref(bp);
    }
    return FALSE;
}

static gboolean super_stream_decide_allocation(GstBaseSrc *bsrc, GstQuery *query)
{
    gboolean ret;
    SuperStream *stream = GST_SUPERSTREAM(bsrc);

    ret = super_stream_decide_allocation_helper(stream, query);
    if (ret) {
        ret = GST_BASE_SRC_CLASS(parent_class)->decide_allocation(bsrc,
                                                                  query);
    }

    if (ret) {
        ret = gst_buffer_pool_set_active(GST_BUFFER_POOL(stream->pool),
                                         TRUE);
        if (!ret) {
            GST_ELEMENT_ERROR(stream, RESOURCE, SETTINGS,
                (("Failed to allocate required memory.")),
                ("Buffer pool activation failed"));

            return FALSE;
        }
    }

    return ret;
}

static gboolean super_stream_query(GstBaseSrc *bsrc, GstQuery *query)
{
    gboolean res = FALSE;

    SuperStream *stream = GST_SUPERSTREAM(bsrc);
    GstQueryType query_type = GST_QUERY_TYPE(query);

    if (query_type == GST_QUERY_LATENCY) {
        res = TRUE;
    } else {
        res = GST_BASE_SRC_CLASS(parent_class)->query (bsrc, query);
    }

    return res;
}

static gboolean super_stream_start(GstBaseSrc *src)
{
    SuperStream *stream = GST_SUPERSTREAM(src);

    stream->frame_cnt = 0;

    stream->ctrl_time = 0;
    gst_object_sync_values(GST_OBJECT(src), stream->ctrl_time);

    return TRUE;
}

static gboolean super_stream_stop(GstBaseSrc *src)
{
    gboolean res = TRUE;

    SuperStream *stream = GST_SUPERSTREAM(src);

    if (stream->pool &&
        gst_buffer_pool_is_active((GstBufferPool *) stream->pool)) {
        res = super_stream_stop_pool(stream);
        if (!res) {
            return res;
        }
    }

    return res;
}

static gboolean super_stream_unlock(GstBaseSrc *src)
{
    SuperStream *stream = GST_SUPERSTREAM(src);

    GST_LOG_OBJECT(stream, "Start flushing");

    if (stream->pool &&
        gst_buffer_pool_is_active((GstBufferPool *) stream->pool)) {

        gst_buffer_pool_set_flushing((GstBufferPool *) stream->pool, TRUE);
    }

    return TRUE;
}

static gboolean super_stream_unlock_stop(GstBaseSrc *src)
{
    SuperStream *stream = GST_SUPERSTREAM(src);

    GST_LOG_OBJECT(stream, "Stop flushing");

    if (stream->pool &&
        gst_buffer_pool_is_active((GstBufferPool *) stream->pool)) {

        gst_buffer_pool_set_flushing((GstBufferPool *) stream->pool, FALSE);
    }

    return TRUE;
}

gboolean super_stream_is_linked(SuperStream *str)
{
    GstPad *peer = NULL;
    GstObject *cam_bin_proxy_pad = NULL;

    g_return_val_if_fail(str != NULL, FALSE);

    peer = gst_pad_get_peer(GST_BASE_SRC_PAD(str));
    if (!peer) {
        GST_ERROR_OBJECT(str, "Cannot get peer pad!");
        return FALSE;
    }

    /* peer's parent is the proxy ghost pad of superhal3cam bin element */
    cam_bin_proxy_pad = gst_pad_get_parent(peer);
    if (!cam_bin_proxy_pad) {
        GST_ERROR_OBJECT(str, "Cannot get parent object of peer pad!");
        return FALSE;
    }

    return gst_pad_is_linked(GST_PAD(cam_bin_proxy_pad));
}

static GstStateChangeReturn super_stream_change_state(GstElement *element,
                                                     GstStateChange transition)
{
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
    SuperStream *stream = GST_SUPERSTREAM(element);
    int res;

    /* State change should only be completed for sources, whose pad's
       corresponding ghostPad in superhal3bin bin is linked to a downstream
       element */
    if (!super_stream_is_linked(stream)) {
        GST_INFO_OBJECT(stream, "%s is not linked to a ghost pad"
            " of SuperHal3Cam bin, which is linked to a downstream element -"
            " will not change state",
            stream->pushsrc.parent.element.object.name);
        return ret;
    }

    if (transition == GST_STATE_CHANGE_NULL_TO_READY) {
        GST_INFO_OBJECT(stream, "Changing state null to ready for source %s",
                        stream->pushsrc.parent.element.object.name);
    }

    ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

    if (transition == GST_STATE_CHANGE_READY_TO_NULL) {
        GST_INFO_OBJECT(stream, "Changing state to ready to null for"
                        " source %s",
                        stream->pushsrc.parent.element.object.name);

        gst_caps_replace(&stream->probed_caps, NULL);

        if (stream->formats) {
            super_stream_clear_formats_list(stream);
            stream->formats = NULL;
        }
    }

    return ret;
}

static GstFlowReturn super_stream_create_buff(GstPushSrc *src, GstBuffer **buf)
{
    GstFlowReturn ret;
    GstMessage *qos_msg;
    GstClockTime ts, absolute_time = GST_CLOCK_TIME_NONE,
        basetime = GST_CLOCK_TIME_NONE, delay = 0;
    GstClock *clock;
    GTimeVal g_now;
    struct timespec now;
    GstClockTime gst_now;
    SuperStream *stream = GST_SUPERSTREAM(src);

    ret = GST_BASE_SRC_CLASS(parent_class)->alloc(GST_BASE_SRC(src), 0,
                                                  stream->info.size, buf);

    if (G_UNLIKELY(ret != GST_FLOW_OK) && ret != GST_FLOW_FLUSHING) {
        GST_ELEMENT_ERROR(src, RESOURCE, NO_SPACE_LEFT, ("Failed to allocate a"
                          " buffer"), (NULL));
        return ret;
    }

    GST_OBJECT_LOCK(stream);

    clock = GST_ELEMENT_CLOCK(stream);
    if (clock) {
        basetime = GST_ELEMENT(stream)->base_time;
        gst_object_ref(clock);
        absolute_time = gst_clock_get_time(clock);
        gst_object_unref(clock);
    }

    ts = absolute_time - basetime;

    GST_OBJECT_UNLOCK(stream);

    stream->ctrl_time = ts;

    gst_object_sync_values(GST_OBJECT(src), stream->ctrl_time);

    if (!GST_BUFFER_OFFSET_IS_VALID(*buf) ||
        !GST_BUFFER_OFFSET_END_IS_VALID(*buf)) {

        GST_BUFFER_OFFSET(*buf) = stream->frame_cnt++;
        GST_BUFFER_OFFSET_END(*buf) = stream->frame_cnt;
    }

    GST_INFO_OBJECT(src, "sync to %" GST_TIME_FORMAT " out ts %"
                    GST_TIME_FORMAT" current frame %llu",
                    GST_TIME_ARGS(stream->ctrl_time), GST_TIME_ARGS(ts),
                    GST_BUFFER_OFFSET(*buf));

    GST_BUFFER_TIMESTAMP(*buf) = ts;
    GST_BUFFER_DURATION(*buf) = GST_CLOCK_TIME_NONE;

    return ret;
}

static GstCaps *super_stream_get_all_caps()
{
    static GstCaps *s_caps = NULL;

    if (g_once_init_enter(&s_caps)) {
        guint i;
        GstStructure *structure;
        GstCaps *all_caps = gst_caps_new_empty();

        for (i = 0; i < SUPER_STREAM_FORMAT_COUNT; i++) {
            structure = super_stream_halpixelformat_to_gst_struct(
                super_formats[i]);

            if (structure) {
                gst_structure_set(structure, "width", GST_TYPE_INT_RANGE,
                    1, SUPER_CAM_MAX_SIZE, "height", GST_TYPE_INT_RANGE, 1,
                    SUPER_CAM_MAX_SIZE, "framerate", GST_TYPE_FRACTION_RANGE,
                    0, 1, G_MAXINT, 1, NULL);
                gst_caps_append_structure(all_caps, structure);
            }
        }
        super_stream_set_all_features(all_caps);

        all_caps = gst_caps_simplify(all_caps);

        g_once_init_leave(&s_caps, all_caps);
    }

    return s_caps;
}

gboolean super_stream_set_num_buffers(SuperStream *str, const GValue *value)
{
    g_return_val_if_fail(str != NULL, FALSE);
    g_return_val_if_fail(value != NULL, FALSE);

    g_object_set_property(G_OBJECT(str), "num-buffers", value);

    return TRUE;
}

gint super_stream_get_num_buffers(SuperStream *vid)
{
    GValue value = { 0 };
    gint ret;

    g_return_val_if_fail(vid != NULL, FALSE);

    g_value_init(&value, G_TYPE_INT);

    g_object_get_property(G_OBJECT(vid), "num-buffers", &value);

    ret = g_value_get_int(&value);

    g_value_unset (&value);

    return ret;
}

static void superstream_class_init(SuperStreamClass *class)
{
    GObjectClass *gobject_class;
    GstElementClass *element_class;
    GstBaseSrcClass *basesrc_class;
    GstPushSrcClass *pushsrc_class;

    gobject_class = G_OBJECT_CLASS(class);
    element_class = GST_ELEMENT_CLASS(class);
    basesrc_class = GST_BASE_SRC_CLASS(class);
    pushsrc_class = GST_PUSH_SRC_CLASS(class);

    gobject_class->finalize = (GObjectFinalizeFunc) super_stream_finalize;

    element_class->change_state = super_stream_change_state;

    gst_element_class_set_static_metadata(element_class,
        "Video Source", "Source/Video",
        "Streams frames from a <placeholder-for-camera>",
        "Super");

    gst_element_class_add_pad_template(element_class,
        gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                             super_stream_get_all_caps()));

    basesrc_class->get_caps = GST_DEBUG_FUNCPTR(super_stream_get_pad_caps);
    basesrc_class->set_caps = GST_DEBUG_FUNCPTR(super_stream_set_caps);
    basesrc_class->start = GST_DEBUG_FUNCPTR(super_stream_start);
    basesrc_class->unlock = GST_DEBUG_FUNCPTR(super_stream_unlock);
    basesrc_class->unlock_stop = GST_DEBUG_FUNCPTR(super_stream_unlock_stop);
    basesrc_class->stop = GST_DEBUG_FUNCPTR(super_stream_stop);
    basesrc_class->query = GST_DEBUG_FUNCPTR(super_stream_query);
    basesrc_class->fixate = GST_DEBUG_FUNCPTR(super_stream_fixate);
    basesrc_class->negotiate = GST_DEBUG_FUNCPTR(super_stream_negotiate);
    basesrc_class->decide_allocation =
        GST_DEBUG_FUNCPTR(super_stream_decide_allocation);

    pushsrc_class->create = GST_DEBUG_FUNCPTR(super_stream_create_buff);
}
