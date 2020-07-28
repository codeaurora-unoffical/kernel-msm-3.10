#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gstplugin.h>
#include <stdio.h>
#include "superhal3src.h"

#define GST_LICENSE "LGPL"
#define GST_PACKAGE_NAME "Super HAL3 Camera"
#define GST_PACKAGE_ORIGIN "SUPER"

static gboolean plugin_init(GstPlugin *plugin)
{
    gboolean res = gst_element_register(plugin, "superhal3src",
        GST_RANK_PRIMARY, GST_TYPE_SUPERHAL3SRC);
    if (!res) {
        return FALSE;
    }

    return TRUE;
}
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  supercamera,
                  "HAL3 based camera source element",
                  plugin_init,
                  VERSION,
                  GST_LICENSE,
                  GST_PACKAGE_NAME,
                  GST_PACKAGE_ORIGIN);

