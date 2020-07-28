#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <dlfcn.h>

#include <glib.h>
#include <glib/gprintf.h>

#include "qcomhal3cam.h"

#define CAMERA_HAL_LIB "/usr/lib64/hw/camera.qcom.so"
#define CAMERA_CONS_USAGE GRALLOC1_CONSUMER_USAGE_NONE
#define PENDING_BUFF_QUEUE_TIMEOUT_US (66000)

typedef enum
{
    PROP_0 = 0,
    PROP_NUM_FRAMES,
    PROP_CAMERA_ID,
    PROP_LIST_END
} QcomProp;

#define qcomhal3cam_parent_class parent_class
G_DEFINE_TYPE(QcomHal3Cam, qcomhal3cam, GST_TYPE_BIN);

static void qcomhal3cam_notify(const struct camera3_callback_ops *ops,
    const camera3_notify_msg_t *msg)
{
}

static void qcomhal3cam_camera_device_status_change(
    const struct camera_module_callbacks* callbacks,
    int camera_id,
    int new_status)
{
}

static void qcomhal3cam_torch_mode_status_change(
    const struct camera_module_callbacks* callbacks,
    const char* camera_id,
    int new_status)
{
}

camera_module_callbacks_t g_module_callbacks = {
    qcomhal3cam_camera_device_status_change,
    qcomhal3cam_torch_mode_status_change
};

static void qcomhal3cam_process_capture_result(
    const struct camera3_callback_ops *ops,
    const camera3_capture_result_t *result)
{
    CallbackOps *cb_ops = (CallbackOps *)ops;
    QcomHal3Cam *cam = cb_ops->parent;
    QcomBufferPool *cur_pool;
    camera3_stream_buffer_t *inflight_buff;
    int i, j;

    if (!result->num_output_buffers) {
        /* result for metadata or input buf..  */
        GST_INFO_OBJECT(cam, "Capture result does not have output bufs -"
                        " returning\n");
        return;
    }

    for (i = 0; i < cam->num_streams; ++i) {

        cur_pool = GST_QCOMSTREAM(cam->streams[i])->pool;

        if (!cur_pool) {
            GST_ERROR_OBJECT(cam, "No buffer pool!");
            continue;
        }

        for (j = 0; j < result->num_output_buffers; ++j) {
            if (result->output_buffers[j].stream == cur_pool->output_stream) {

                inflight_buff = (camera3_stream_buffer_t *)
                    g_async_queue_pop(cur_pool->inflight);

                if (cur_pool->cur_fr == result->frame_number) {
                    camera3_stream_t *str = inflight_buff->stream;
                    cur_pool->cur_fr++;

                    GST_INFO_OBJECT(cam ,"Capture result for %s: processed"
                        " frame %u buffer %p of stream %p %ux%u fmt %u usage"
                        " %u", cur_pool->parent.object.name,
                        result->frame_number, *inflight_buff->buffer, str,
                        str->width, str->height, str->format, str->usage);

                    g_async_queue_push(cur_pool->processed, inflight_buff);
                } else {
                    GST_WARNING_OBJECT(cam, "Frame counter mismatch! %u != %u"
                        " for pool %p %s", cur_pool->cur_fr,
                        result->frame_number, cur_pool,
                        cur_pool->parent.object.name);
                }
            }
        }
    }
}

static void *qcomhal3cam_cr_thread_function(void *prm)
{
    CRThreadData *data = (CRThreadData *)prm;
    CRThreadMessage *msg = NULL;
    camera3_capture_request_t pending_cr;
    camera3_stream_buffer_t *pending_out_buff;
    camera3_stream_buffer_t all_out_buffs[MAX_STREAMS];
    camera3_stream_buffer_t *all_out_buff_ptrs[MAX_STREAMS];
    camera_metadata_t *preview_req_settings = NULL;
    //camera_metadata_t *video_req_settings = NULL;
    uint32_t acquired_buffs, framenum = 0;
    int i, ret;

    pthread_mutex_lock(&data->device_lock);
    preview_req_settings = (camera_metadata_t *)
        data->device->ops->construct_default_request_settings(data->device,
        CAMERA3_TEMPLATE_PREVIEW);
    pthread_mutex_unlock(&data->device_lock);
    if (!preview_req_settings) {
        GST_ERROR("Cannot get preview CR settings!");
        return NULL;
    }
#if 0
    pthread_mutex_lock(&data->device_lock);
    video_req_settings = (camera_metadata_t *)
        data->device->ops->construct_default_request_settings(data->device,
        CAMERA3_TEMPLATE_VIDEO_RECORD);
    pthread_mutex_unlock(&data->device_lock);
    if (!video_req_settings) {
        GST_ERROR("Cannot get video CR settings!");
        return NULL;
    }
#endif

    while (1) {
        acquired_buffs = 0;
        i = 0;
        memset(all_out_buffs, 0, sizeof(all_out_buffs));
        memset(all_out_buff_ptrs, 0, sizeof(all_out_buff_ptrs));
        memset(&pending_cr, 0, sizeof(pending_cr));

        while (acquired_buffs < data->num_streams) {
            msg = (CRThreadMessage *) g_async_queue_try_pop(data->msg_q);

            if (msg && msg->t == STOP) {
                GST_INFO("STOP message arrived in CR worker thread!");
                pthread_exit(NULL);
            }

            if (all_out_buff_ptrs[i]) {
                i = i < data->num_streams - 1 ? i + 1 : 0;
                continue;
            }

            pending_out_buff = (camera3_stream_buffer_t *)
                g_async_queue_timeout_pop(data->pools[i]->pending,
                                          PENDING_BUFF_QUEUE_TIMEOUT_US);
            if (pending_out_buff) {
                pending_cr.input_buffer = NULL;
                pending_cr.settings = preview_req_settings;
#if 0
                if (data->num_streams > 1) {
                    pending_cr.settings = video_req_settings;
                }
#endif
                all_out_buffs[i] = *pending_out_buff;

                all_out_buff_ptrs[i] = pending_out_buff;
                pending_cr.num_output_buffers++;
                acquired_buffs++;
            }

            i = i < data->num_streams - 1 ? i + 1 : 0;
        }

        pending_cr.input_buffer = NULL;
        pending_cr.output_buffers = all_out_buffs;

        pending_cr.frame_number = framenum++;
        GST_INFO("process_capture_request for frame %u w/ %u out buffs",
                 pending_cr.frame_number, pending_cr.num_output_buffers);
        for (i = 0; i < pending_cr.num_output_buffers; ++i) {
            camera3_stream_t *str = all_out_buffs[i].stream;
            GST_INFO("CR contains out_buffs[%d] %p w/ stream %p %ux%u fmt %u"
                     " usage %u", i, *all_out_buffs[i].buffer, str, str->width,
                     str->height, str->format, str->usage);
        }

        pthread_mutex_lock(&data->device_lock);
        ret = data->device->ops->process_capture_request(data->device,
                                                         &pending_cr);
        if (ret != 0) {
            pthread_mutex_unlock(&data->device_lock);
            GST_ERROR("CR Thread: error %d on process_capture_request!", ret);
            /* output buffer does not go to inflight queue - instead it should
               be returned to pool's pending queue */
            for (i = 0; i < pending_cr.num_output_buffers; ++i) {
                if (all_out_buff_ptrs[i]) {
                    g_async_queue_push(data->pools[i]->pending,
                                       all_out_buff_ptrs[i]);
                }
            }
            continue;
        }
        pthread_mutex_unlock(&data->device_lock);

        for (i = 0; i < pending_cr.num_output_buffers; ++i) {
            if (all_out_buff_ptrs[i]) {
                g_async_queue_push(data->pools[i]->inflight,
                                   all_out_buff_ptrs[i]);
            }
        }
    }
}

static gboolean qcomhal3cam_bp_created(gpointer data)
{
    QcomHal3Cam *cam = GST_QCOMHAL3CAM_CAST(data);

    g_return_val_if_fail(cam != NULL, FALSE);

    pthread_mutex_lock(&cam->pushsrc_sync.lock);

    cam->pushsrc_sync.created_pools++;

    if (cam->pushsrc_sync.created_pools == cam->num_streams) {
        pthread_cond_broadcast(&cam->pushsrc_sync.wait_pools_created);
    }

    pthread_mutex_unlock(&cam->pushsrc_sync.lock);

    return TRUE;
}

static void qcomhal3cam_wait_buffer_pools(QcomHal3Cam *cam)
{
    pthread_mutex_lock(&cam->pushsrc_sync.lock);

    if (cam->pushsrc_sync.created_pools < cam->num_streams) {
        GST_INFO_OBJECT(cam, "cam bin %s is waiting for buffer pools",
            cam->bin.element.object.name);
        pthread_cond_wait(&cam->pushsrc_sync.wait_pools_created,
                          &cam->pushsrc_sync.lock);
        GST_INFO_OBJECT(cam, "cam bin %s is ready for configure_streams!",
            cam->bin.element.object.name);
    }

    pthread_mutex_unlock(&cam->pushsrc_sync.lock);
}

gboolean qcomhal3cam_create_output(QcomHal3Cam *cam, const gchar *elem_name,
    const guint idx)
{
    GstPad *static_pad = NULL;
    GstElement *output = NULL;
    GstPad *ghost_pad = NULL;
    gboolean ret = TRUE;

    g_return_val_if_fail(cam != NULL, FALSE);
    g_return_val_if_fail(elem_name != NULL, FALSE);

    output = qcom_stream_create(cam, elem_name, qcomhal3cam_bp_created,
                                cam->cam_id);
    if (!output) {
        GST_ERROR_OBJECT(cam, "Could not create %s!", elem_name);
        return FALSE;
    }

    /* Adding stream to bin */
    ret = gst_bin_add(GST_BIN_CAST(cam), gst_object_ref(output));
    if(!ret) {
        GST_ERROR_OBJECT(cam, "Could not add %s in bin!", elem_name);
        return FALSE;
    }

    cam->streams[idx] = output;
    GST_QCOMSTREAM(cam->streams[idx])->usage = CAMERA_CONS_USAGE;

    return ret;
}

static gboolean qcomhal3cam_create_streams(QcomHal3Cam *cam)
{
    int i, res;

    /* Create all QcomStream outputs */
    for (i = 0; i < MAX_STREAMS; ++i) {
        int elem_name_len = NAME_LEN / 4;
        gchar stream_name[elem_name_len];
        res = snprintf(stream_name, elem_name_len, "stream_%d", i);
        if (res < 0 || res == elem_name_len) {
            GST_ERROR_OBJECT(cam, "Could not write element name of output %d",
                             i);
            goto error;
        }

        res = qcomhal3cam_create_output(cam, stream_name, i);
        if (res != TRUE) {
            goto error;
        }
    }

    return TRUE;

error:
    return FALSE;
}

static void qcomhal3cam_init(QcomHal3Cam *cam)
{
    cam->cb_ops.ops.process_capture_result =
        qcomhal3cam_process_capture_result;
    cam->cb_ops.ops.notify = qcomhal3cam_notify;
    cam->cb_ops.parent = gst_object_ref(cam);

    if (qcomhal3cam_create_streams(cam) != TRUE) {
        GST_ERROR_OBJECT(cam, "Failed to create qcomhal3cam_elements!");
    }
}

static int qcomhal3cam_open_hal(QcomHal3Cam *cam)
{
    int ret;
    void *lib_hndl = NULL;

    lib_hndl = dlopen(CAMERA_HAL_LIB, RTLD_NOW);
    if (!lib_hndl) {
        GST_ERROR_OBJECT(cam, "Cannot load camera HAL lib %s: %s",
                         CAMERA_HAL_LIB, dlerror());
        return -ENOENT;
    }

    cam->module = (camera_module_t *) dlsym(lib_hndl,
                                            HAL_MODULE_INFO_SYM_AS_STR);
    if (!cam->module) {
        GST_ERROR_OBJECT(cam, "Cannot load camera HAL module info %s: %s",
                         HAL_MODULE_INFO_SYM_AS_STR, dlerror());
        dlclose(lib_hndl);
        return -EIO;
    }

    cam->module->common.dso = lib_hndl;
    ret = cam->module->init();
    if (ret < 0) {
        GST_ERROR_OBJECT(cam, "Cannot init camera HAL module!");
        dlclose(lib_hndl);
        return -EIO;
    }

    ret = cam->module->set_callbacks(&g_module_callbacks);
    if (ret < 0) {
        GST_ERROR_OBJECT(cam, "Cannot set camera HAL callbacks!");
        dlclose(lib_hndl);
        return -EIO;
    }

    return ret;
}

static gboolean qcomhal3cam_open_camera(QcomHal3Cam *cam)
{
    int ret;
    char camera_name[20] = {0};
    sprintf(camera_name, "%d", cam->cam_id);

    ret = cam->module->common.methods->open(&cam->module->common,
        camera_name, (hw_device_t**)(&cam->device));
    if (ret || !cam->device) {
        GST_ERROR_OBJECT(cam, "Cannot open camera %d!", cam->cam_id);
        return FALSE;
    }

    ret = cam->device->ops->initialize(cam->device, &cam->cb_ops.ops);
    if (ret) {
        GST_ERROR_OBJECT(cam, "Cannot initialize camera device %d!",
                         cam->cam_id);
        return FALSE;
    }

    return TRUE;
}

static gint qcomhal3cam_compare_by_resolution(QcomHALFormat *lhs,
    QcomHALFormat *rhs)
{
    return ((rhs->height * rhs->width) - (lhs->height * lhs->width));
}

static gboolean qcomhal3cam_fill_formats_list(QcomHal3Cam *cam)
{
    int res;
    gint i, ret;
    GList *formats;
    QcomHALFormat *format;
    struct camera_info info;
    camera_metadata_ro_entry_t str_cfgs;
    camera_metadata_t *static_meta;

    if (!CAMERA_HAL_IS_OPEN(cam)) {
        GST_ELEMENT_ERROR(cam, RESOURCE, SETTINGS, (NULL),
                          ("Camera HAL is not open!"));
        return FALSE;
    }

    cam->module->get_camera_info(cam->cam_id, &info);

    static_meta = (camera_metadata_t *) info.static_camera_characteristics;

    ret = find_camera_metadata_ro_entry(static_meta,
        ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &str_cfgs);
    if (ret || (str_cfgs.count % 4) != 0) {
        return FALSE;
    }

    for (i = 0; i < str_cfgs.count; i += 4) {
        if (ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT ==
            str_cfgs.data.i32[i + 3]) {
            format = g_new0(QcomHALFormat, 1);

            switch (str_cfgs.data.i32[i]) {
            case ANDROID_SCALER_AVAILABLE_FORMATS_IMPLEMENTATION_DEFINED:
                /* TODO pix_fmt = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED*/
                format->pix_fmt = HAL_PIXEL_FORMAT_YCBCR_420_888;
                break;
            case ANDROID_SCALER_AVAILABLE_FORMATS_YCbCr_420_888:
                format->pix_fmt = HAL_PIXEL_FORMAT_YCBCR_420_888;
                break;
            case ANDROID_SCALER_AVAILABLE_FORMATS_YCrCb_420_SP:
                format->pix_fmt = HAL_PIXEL_FORMAT_YCRCB_420_SP;
                break;
            case ANDROID_SCALER_AVAILABLE_FORMATS_YV12:
                format->pix_fmt = HAL_PIXEL_FORMAT_YV12;
                break;
            default:
                GST_ERROR_OBJECT(cam, "Unsupported format %d!",
                                 str_cfgs.data.i32[i]);
                break;
            }
            format->width = str_cfgs.data.i32[i + 1];
            format->height = str_cfgs.data.i32[i + 2];

            cam->formats = g_list_append(cam->formats, format);
        }
    }

    cam->formats = g_list_sort(cam->formats,
        (GCompareFunc) qcomhal3cam_compare_by_resolution);

    for (formats = cam->formats; formats != NULL; formats = formats->next) {
        format = (QcomHALFormat *)formats->data;
        GST_INFO_OBJECT(cam, "Got HAL Format %d %ux%u", format->pix_fmt,
                        format->width, format->height);
    }

    return TRUE;
}

static gboolean qcomhal3cam_distribute_formats(QcomHal3Cam *cam)
{
    gboolean res;
    gint i;
    QcomStream *cur_str;

    for (i = 0; i < MAX_STREAMS; ++i) {
        cur_str = GST_QCOMSTREAM(cam->streams[i]);

        res = qcom_stream_save_formats(cur_str, cam->formats);
        if (res != TRUE) {
            GST_ERROR_OBJECT(cam, "Could not copy formats list!");
            return res;
        }
    }

    return res;
}

static gboolean qcomhal3cam_configure_streams(QcomHal3Cam *cam)
{
    gint i, res;
    QcomStream *cur_str = NULL;
    camera3_stream_t *cur_output_stream = NULL;
    camera3_stream_t *streams[MAX_STREAMS];
    camera3_stream_configuration_t stream_config;

    stream_config.operation_mode = CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE;
    stream_config.num_streams = 0;

    for (i = 0; i < cam->num_streams; ++i) {
        cur_str = GST_QCOMSTREAM(cam->streams[i]);

        cur_output_stream = qcom_buffer_pool_get_stream(cur_str->pool);
        if (!cur_output_stream) {
            GST_WARNING_OBJECT(cam, "Missing output stream %d, probably not"
                               " linked to a downstream element !", i);
            continue;
        }
        streams[i] = cur_output_stream;
        stream_config.num_streams++;
        GST_INFO_OBJECT(cam, "Added stream %ux%u fmt %d to configure_streams"
                        " configuration", cur_output_stream->width,
                        cur_output_stream->height, cur_output_stream->format);
    }

    if (stream_config.num_streams < 1) {
        GST_ERROR_OBJECT(cam, "No output streams acquired!");
        return FALSE;
    }

    stream_config.streams = streams;
    stream_config.session_parameters = NULL;

    pthread_mutex_lock(&cam->crt_data.device_lock);
    res = cam->device->ops->configure_streams(cam->device,
                                              &stream_config);
    pthread_mutex_unlock(&cam->crt_data.device_lock);
    if (res) {
        GST_ERROR_OBJECT(cam, "configure_streams failed: %d", res);
        return FALSE;
    }

    return TRUE;
}

static gboolean qcomhal3cam_init_cr_thread(QcomHal3Cam *cam)
{
    int i, res;

    g_return_val_if_fail(cam != NULL, FALSE);

    cam->crt_data.msg_q = g_async_queue_new();
    cam->crt_data.device = cam->device;
    cam->crt_data.num_streams = cam->num_streams;
    pthread_mutex_init(&cam->crt_data.device_lock, NULL);

    for (i = 0; i < cam->num_streams; ++i) {
        cam->crt_data.pools[i] =
            gst_object_ref(GST_QCOMSTREAM(cam->streams[i])->pool);
    }

    res = pthread_create(&cam->cr_thread, NULL,
                         qcomhal3cam_cr_thread_function,
                         &cam->crt_data);
    if (res < 0) {
        GST_ELEMENT_ERROR(cam, RESOURCE, FAILED,
                          ("Error! Cannot create CR worker thread!: %s",
                          strerror(errno)), GST_ERROR_SYSTEM);

        g_async_queue_unref(cam->crt_data.msg_q);
        cam->crt_data.msg_q = NULL;

        return FALSE;
    }

    return TRUE;
}

static gboolean qcomhal3cam_deinit_cr_thread(QcomHal3Cam *cam)
{
    int i, ret;
    CRThreadMessage exit_msg = {STOP, NULL};

    g_return_val_if_fail(cam != NULL, FALSE);

    g_async_queue_push(cam->crt_data.msg_q, &exit_msg);

    ret = pthread_join(cam->cr_thread, NULL);
    if (ret) {
        GST_ELEMENT_ERROR(cam, RESOURCE, FAILED, ("Error! Failed to join CR"
            " generating thread: %s!", strerror(ret)), GST_ERROR_SYSTEM);
        return FALSE;
    }

    for (i = 0; i < cam->num_streams; ++i) {
        if (cam->crt_data.pools[i]) {
            gst_object_unref(cam->crt_data.pools[i]);
            cam->crt_data.pools[i] = NULL;
        }
    }

    pthread_mutex_destroy(&cam->crt_data.device_lock);

    return TRUE;
}

static gboolean qcomhal3cam_stop_camera(QcomHal3Cam *cam)
{
    gint res;

    if (cam) {
        res = qcomhal3cam_deinit_cr_thread(cam);
        if (res != TRUE) {
            GST_ERROR_OBJECT(cam, "CR thread deinit failure!");
            return FALSE;
        }

        pthread_mutex_lock(&cam->crt_data.device_lock);
        /* flush will block until all inflight CR's results are returned - at
         * least that is the observed bahviour */
        res = cam->crt_data.device->ops->flush(cam->crt_data.device);
        if (res) {
            GST_ERROR_OBJECT(cam, "Camera flush failure!");
            pthread_mutex_unlock(&cam->crt_data.device_lock);
            return FALSE;
        }

        cam->crt_data.device->common.close(&cam->crt_data.device->common);
        // nothing we can do if close returns error anyway...
        pthread_mutex_unlock(&cam->crt_data.device_lock);
    }

    return TRUE;
}

static gboolean qcomhal3cam_start_camera(QcomHal3Cam *cam)
{
    gboolean ret = TRUE;

    if (cam) {
        ret = qcomhal3cam_configure_streams(cam);
        if (ret != TRUE) {
            return ret;
        }

        ret = qcomhal3cam_init_cr_thread(cam);
        if (ret != TRUE) {
            return ret;
        }
    }

    return ret;
}

static void qcomhal3cam_clear_formats_list(QcomHal3Cam *cam)
{
    g_list_foreach(cam->formats, (GFunc) g_free, NULL);
    g_list_free(cam->formats);
    cam->formats = NULL;
}

static gboolean qcomlhal3cam_has_linked_pads(QcomHal3Cam *cam)
{
    gint i;
    gboolean linked;

    for (i = 0; i < MAX_STREAMS; ++i) {
        linked = qcom_stream_is_linked(GST_QCOMSTREAM(cam->streams[i]));
        if (linked) {
            return TRUE;
        }
    }

    return FALSE;
}

static GstStateChangeReturn qcomhal3cam_change_state(GstElement *element,
                                                    GstStateChange transition)
{
    int i, res;
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
    GstStateChangeReturn tmp;
    QcomHal3Cam *cam = GST_QCOMHAL3CAM_CAST(element);
    QcomStream *cur_str = NULL;

    if (!qcomlhal3cam_has_linked_pads(cam)) {
        GST_INFO_OBJECT(cam, "Camera %s does not have pads, linked to any"
                        " downstream element - will not change state",
                        cam->bin.element.object.name);
        return ret;
    }

    if (transition == GST_STATE_CHANGE_NULL_TO_READY) {
        if (!CAMERA_HAL_IS_OPEN(cam)) {
            res = qcomhal3cam_open_hal(cam);
            if (res) {
                return GST_STATE_CHANGE_FAILURE;
            }
        }

        res = qcomhal3cam_open_camera(cam);
        if (res != TRUE) {
            return GST_STATE_CHANGE_FAILURE;
        }

        res = qcomhal3cam_fill_formats_list(cam);
        if (res != TRUE) {
            return GST_STATE_CHANGE_FAILURE;
        }

        res = qcomhal3cam_distribute_formats(cam);
        if (res != TRUE) {
            return GST_STATE_CHANGE_FAILURE;
        }

        pthread_mutex_init(&cam->pushsrc_sync.lock, NULL);
        pthread_cond_init(&cam->pushsrc_sync.wait_pools_created, NULL);
    }

    if (transition == GST_STATE_CHANGE_PAUSED_TO_PLAYING) {
        for (i = 0; i < MAX_STREAMS; ++i) {
            cur_str = GST_QCOMSTREAM(cam->streams[i]);
            gboolean linked = qcom_stream_is_linked(cur_str);
            if (linked) {
                cam->num_streams++;
            }
            GST_INFO_OBJECT(cam, "Stream %s is linked %d",
                cur_str->pushsrc.parent.element.object.name, linked);
        }
    }

    ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

    if (transition == GST_STATE_CHANGE_PAUSED_TO_PLAYING) {
        /* wait for streams to create buffer pools before configure_streams */
         qcomhal3cam_wait_buffer_pools(cam);
    }

    if (transition == GST_STATE_CHANGE_PAUSED_TO_PLAYING) {

        res = qcomhal3cam_start_camera(cam);
        if (res != TRUE) {
            GST_ERROR_OBJECT(cam, "Cannot start camera!");
            return GST_STATE_CHANGE_FAILURE;
        }
    }

    if (transition == GST_STATE_CHANGE_PLAYING_TO_PAUSED) {

        res = qcomhal3cam_stop_camera(cam);
        if (res != TRUE) {
            GST_ERROR_OBJECT(cam, "Cannot stop camera!");
            return GST_STATE_CHANGE_FAILURE;
        }
    }

    if (transition == GST_STATE_CHANGE_READY_TO_NULL) {
        if (cam->formats) {
            qcomhal3cam_clear_formats_list(cam);
        }

        pthread_mutex_destroy(&cam->pushsrc_sync.lock);
        pthread_cond_destroy(&cam->pushsrc_sync.wait_pools_created);
    }

    return ret;
}

static void qcomhal3cam_dispose(GObject *object)
{
    QcomHal3Cam *cam = GST_QCOMHAL3CAM_CAST(object);

    if (cam->formats) {
        qcomhal3cam_clear_formats_list(cam);
    }

    if (cam->crt_data.msg_q) {
        g_async_queue_unref(cam->crt_data.msg_q);
        cam->crt_data.msg_q = NULL;
    }

    if (cam->cb_ops.parent) {
        gst_object_unref(cam->cb_ops.parent);
    }

    G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void qcomhal3cam_finalize(GObject *object)
{
    QcomHal3Cam *cam = GST_QCOMHAL3CAM_CAST(object);

    if (cam->formats) {
        qcomhal3cam_clear_formats_list(cam);
    }

    if (cam->crt_data.msg_q) {
        g_async_queue_unref(cam->crt_data.msg_q);
        cam->crt_data.msg_q = NULL;
    }

    if (cam->cb_ops.parent) {
        gst_object_unref(cam->cb_ops.parent);
    }

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void qcomhal3cam_class_init(QcomHal3CamClass *cam_class) {

    GObjectClass *gobject_class;
    GstElementClass *element_class;

    gobject_class = G_OBJECT_CLASS(cam_class);
    element_class = GST_ELEMENT_CLASS(cam_class);

    gobject_class->dispose = qcomhal3cam_dispose;
    gobject_class->finalize = qcomhal3cam_finalize;

    element_class->change_state = GST_DEBUG_FUNCPTR(qcomhal3cam_change_state);
}
