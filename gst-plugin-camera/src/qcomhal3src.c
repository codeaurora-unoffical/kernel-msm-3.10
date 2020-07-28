/**
 * SECTION:element-qcomhal3src
 *
 * FIXME:Describe qcomhal3src here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v -m qcomhal3src ! filesink location=~/test.yuv
 * </refsect2>
 */

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <glib.h>
#include <glib/gprintf.h>

#include "qcomhal3src.h"

// TODO TODO define a debug category!!!! for reference look at redmine task

#define qcomhal3src_parent_class parent_class

typedef enum
{
    PROP_0 = 0,
    PROP_NUM_FRAMES,
    PROP_CAMERA_ID,
    PROP_LIST_END
} QcomProp;

#define qcomstream_parent_class parent_class
G_DEFINE_TYPE(QcomHal3Src, qcomhal3src, GST_TYPE_BIN);

gboolean qcomhal3src_create_camera(QcomHal3Src *src, const gchar *elem_name,
    const guint cam_idx)
{
    GstPad *static_pad = NULL;
    GstElement *camera = NULL;
    QcomHal3Cam *cam;
    GstPad *ghost_pad = NULL;
    gboolean ret = TRUE;
    uint str_idx;
    gint res;

    g_return_val_if_fail(src != NULL, FALSE);
    g_return_val_if_fail(elem_name != NULL, FALSE);

    camera = g_object_new(GST_TYPE_QCOMHAL3CAM, "name", elem_name, NULL);
    if (!camera) {
        GST_ERROR_OBJECT(src, "Could not create %s!", elem_name);
        return FALSE;
    }

    cam = GST_QCOMHAL3CAM_CAST(camera);
    cam->cam_id = cam_idx;

    for (str_idx = 0; str_idx < MAX_STREAMS; ++str_idx) {
        int elem_name_len = NAME_LEN / 4;
        gchar cur_pad_name[elem_name_len];

        res = snprintf(cur_pad_name, elem_name_len, "%s_stream_%d",
                       elem_name, str_idx);
        if (res < 0 || res == elem_name_len) {
            GST_ERROR_OBJECT(src, "Could not write pad name of proxy pad %d",
                             str_idx);
            return FALSE;
        }

        static_pad = gst_element_get_static_pad(cam->streams[str_idx], "src");
        if(!static_pad) {
            GST_ERROR_OBJECT(src, "Failed to get static pad!");
            return FALSE;
        }

        ghost_pad = gst_ghost_pad_new(cur_pad_name, static_pad);
        if (!ghost_pad) {
            GST_ERROR_OBJECT(src, "Failed to create ghost pad %s!", cur_pad_name);
            return FALSE;
        }

        gst_object_unref(GST_OBJECT(static_pad));

        ret = gst_element_add_pad(GST_ELEMENT(src), ghost_pad);
        if (ret == FALSE) {
            GST_ERROR_OBJECT(src, "Failed to add ghost pad %s to %s!",
                             ghost_pad->object.name, camera->object.name);
            return FALSE;
        }

        src->proxy_pads[cam_idx][str_idx] = ghost_pad;
        GST_INFO_OBJECT(src, "Added proxy pad %s to %s", cur_pad_name,
                        camera->object.name);
    }

    /* Adding camera to bin - move after pads are linked */
    ret = gst_bin_add(GST_BIN_CAST(src), gst_object_ref(camera));
    if(!ret) {
        GST_ERROR_OBJECT(src, "Could not add %s in bin!", elem_name);
        return FALSE;
    }

    GST_INFO_OBJECT(src, "Added %s to camera bin", elem_name);

    src->cameras[cam_idx] = camera;

    return ret;
}

static void qcomhal3src_init(QcomHal3Src *src)
{
    gint i, res;

    /* Create all QcomHal3Cam outputs */
    for (i = 0; i < MAX_CAMS; ++i) {
        int elem_name_len = NAME_LEN / 4;
        gchar cur_src_elem_name[elem_name_len];
        res = snprintf(cur_src_elem_name, elem_name_len, "camera_%d", i);
        if (res < 0 || res == elem_name_len) {
            GST_ERROR_OBJECT(src, "Could not write element name of output %d",
                             i);
            return;
        }

        qcomhal3src_create_camera(src, cur_src_elem_name, i);
    }
}

static gboolean qcomhal3src_proxy_pad_is_linked(GstPad *prxy_pad)
{
    GstPad *peer = NULL;
    GstObject *cam_bin_proxy_pad = NULL;

    peer = gst_pad_get_peer(prxy_pad);
    if (!peer) {
        GST_ERROR_OBJECT(prxy_pad, "Cannot get peer pad!");
        return FALSE;
    }

    /* peer's parent is the proxy ghost pad of qcomcam bin element */
    cam_bin_proxy_pad = gst_pad_get_parent(prxy_pad);
    if (!cam_bin_proxy_pad) {
        GST_ERROR_OBJECT(prxy_pad, "Cannot get parent object of peer pad!");
        return FALSE;
    }

    return gst_pad_is_linked(GST_PAD(cam_bin_proxy_pad));
}

static GstStateChangeReturn qcomhal3src_change_state(GstElement *element,
                                                     GstStateChange transition)
{
    gboolean res;
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
    GstStateChangeReturn tmp;
    QcomHal3Src *src = GST_QCOMHAL3SRC_CAST(element);
    QcomStream *cur_str = NULL;

    ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

    if (transition == GST_STATE_CHANGE_PAUSED_TO_PLAYING &&
        ret == GST_STATE_CHANGE_SUCCESS) {
    }

    if (transition == GST_STATE_CHANGE_PAUSED_TO_PLAYING) {

    }

    if (transition == GST_STATE_CHANGE_PLAYING_TO_PAUSED) {
    }

    if (transition == GST_STATE_CHANGE_READY_TO_NULL) {
    }

    return ret;
}

static void qcomhal3src_dispose(GObject *object)
{
    G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void qcomhal3src_finalize(GObject *object)
{
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void qcomhal3src_set_property(GObject *object, guint prop_id,
                                   const GValue *value, GParamSpec *pspec)
{
    QcomHal3Src *src = GST_QCOMHAL3SRC_CAST(object);
    QcomStream *stream = NULL;
    gboolean res = 0;
    int i;

    switch (prop_id) {
    case PROP_NUM_FRAMES:
        break;
    case PROP_CAMERA_ID:
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void qcomhal3src_get_property(GObject *object, guint prop_id,
                                   GValue *value, GParamSpec *pspec)
{
    QcomHal3Src *src = GST_QCOMHAL3SRC_CAST(object);

    gint num_buffers;
    gint tmp;
    gboolean res;

    switch (prop_id) {
    case PROP_NUM_FRAMES:
        break;
    case PROP_CAMERA_ID:
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void qcomhal3src_class_init(QcomHal3SrcClass *src_clas) {

    GObjectClass *gobject_class;
    GstElementClass *element_class;

    gobject_class = G_OBJECT_CLASS(src_clas);
    element_class = GST_ELEMENT_CLASS(src_clas);

    gobject_class->dispose = qcomhal3src_dispose;
    gobject_class->finalize = qcomhal3src_finalize;
    gobject_class->set_property = qcomhal3src_set_property;
    gobject_class->get_property = qcomhal3src_get_property;

    element_class->change_state = GST_DEBUG_FUNCPTR(qcomhal3src_change_state);

    /* Number of frames to limit output to */
    g_object_class_install_property(gobject_class, PROP_NUM_FRAMES,
        g_param_spec_int("num-frames", "Number of frames before EOS",
                         "Number of frames to output before sending EOS"
                         " (-1 = unlimited)", -1, G_MAXINT, -1,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    /* Camera id */
    g_object_class_install_property(gobject_class, PROP_CAMERA_ID,
        g_param_spec_uint("camera-id", "camera-id",
                          "Selected camera id", 0, G_MAXUINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}
