#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gstplugin.h>
#include <stdio.h>
#include "qcomhal3src.h"

#define GST_LICENSE "LGPL"
#define GST_PACKAGE_NAME "Qcom HAL3 Camera"
#define GST_PACKAGE_ORIGIN "QCOM"

static gboolean plugin_init(GstPlugin *plugin)
{
    gboolean res = gst_element_register(plugin, "qcomhal3src",
        GST_RANK_PRIMARY, GST_TYPE_QCOMHAL3SRC);
    if (!res) {
        return FALSE;
    }

    return TRUE;
}
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  qcomcamera,
                  "HAL3 based camera source element",
                  plugin_init,
                  VERSION,
                  GST_LICENSE,
                  GST_PACKAGE_NAME,
                  GST_PACKAGE_ORIGIN);

