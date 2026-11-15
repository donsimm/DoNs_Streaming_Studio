// Hilfsprogramm für kd_v4l2 (P15 a2): gibt ioctl-Nummern, Konstanten, Grössen und
// Offsets der Strukturen aus den Kernel-Headern dieses PCs aus. Der Test vergleicht
// sie mit den Werten in dss_camera/v4l2.py.
// Ausgabe je Zeile: «NAME WERT» (dezimal).
// Quelle der Header: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/videodev.html

#include <linux/uvcvideo.h>
#include <linux/videodev2.h>

#include <cstddef>
#include <cstdio>

namespace {

void print(const char *name, unsigned long long value)
{
    std::printf("%s %llu\n", name, value);
}

} // namespace

#define DSS_VALUE(name) print(#name, static_cast<unsigned long long>(name))
#define DSS_SIZE(type) print("sizeof." #type, sizeof(struct type))
#define DSS_OFFSET(type, field) print("offsetof." #type "." #field, offsetof(struct type, field))

int main()
{
    DSS_VALUE(VIDIOC_QUERYCAP);
    DSS_VALUE(VIDIOC_QUERYCTRL);
    DSS_VALUE(VIDIOC_QUERYMENU);
    DSS_VALUE(VIDIOC_G_CTRL);
    DSS_VALUE(VIDIOC_S_CTRL);
    DSS_VALUE(UVCIOC_CTRL_QUERY);

    DSS_VALUE(V4L2_CAP_VIDEO_CAPTURE);
    DSS_VALUE(V4L2_CAP_META_CAPTURE);
    DSS_VALUE(V4L2_CAP_DEVICE_CAPS);
    DSS_VALUE(V4L2_CTRL_FLAG_NEXT_CTRL);
    DSS_VALUE(V4L2_CTRL_FLAG_DISABLED);
    DSS_VALUE(V4L2_CTRL_FLAG_INACTIVE);
    DSS_VALUE(V4L2_CTRL_TYPE_INTEGER);
    DSS_VALUE(V4L2_CTRL_TYPE_BOOLEAN);
    DSS_VALUE(V4L2_CTRL_TYPE_MENU);
    DSS_VALUE(V4L2_CTRL_TYPE_BUTTON);
    DSS_VALUE(V4L2_CTRL_TYPE_CTRL_CLASS);
    DSS_VALUE(V4L2_CTRL_TYPE_INTEGER_MENU);
    DSS_VALUE(V4L2_CID_BRIGHTNESS);
    DSS_VALUE(V4L2_CID_CONTRAST);
    DSS_VALUE(V4L2_CID_SATURATION);
    DSS_VALUE(V4L2_CID_HUE);
    DSS_VALUE(V4L2_CID_AUTO_WHITE_BALANCE);
    DSS_VALUE(V4L2_CID_GAMMA);
    DSS_VALUE(V4L2_CID_GAIN);
    DSS_VALUE(V4L2_CID_POWER_LINE_FREQUENCY);
    DSS_VALUE(V4L2_CID_WHITE_BALANCE_TEMPERATURE);
    DSS_VALUE(V4L2_CID_SHARPNESS);
    DSS_VALUE(V4L2_CID_BACKLIGHT_COMPENSATION);
    DSS_VALUE(V4L2_CID_EXPOSURE_AUTO);
    DSS_VALUE(V4L2_CID_EXPOSURE_ABSOLUTE);
    DSS_VALUE(V4L2_CID_EXPOSURE_AUTO_PRIORITY);
    DSS_VALUE(V4L2_CID_PAN_ABSOLUTE);
    DSS_VALUE(V4L2_CID_TILT_ABSOLUTE);
    DSS_VALUE(V4L2_CID_FOCUS_ABSOLUTE);
    DSS_VALUE(V4L2_CID_FOCUS_AUTO);
    DSS_VALUE(V4L2_CID_ZOOM_ABSOLUTE);
    DSS_VALUE(V4L2_CTRL_CLASS_USER);
    DSS_VALUE(V4L2_CTRL_CLASS_CAMERA);

    DSS_SIZE(v4l2_capability);
    DSS_OFFSET(v4l2_capability, driver);
    DSS_OFFSET(v4l2_capability, card);
    DSS_OFFSET(v4l2_capability, bus_info);
    DSS_OFFSET(v4l2_capability, version);
    DSS_OFFSET(v4l2_capability, capabilities);
    DSS_OFFSET(v4l2_capability, device_caps);

    DSS_SIZE(v4l2_queryctrl);
    DSS_OFFSET(v4l2_queryctrl, id);
    DSS_OFFSET(v4l2_queryctrl, type);
    DSS_OFFSET(v4l2_queryctrl, name);
    DSS_OFFSET(v4l2_queryctrl, minimum);
    DSS_OFFSET(v4l2_queryctrl, maximum);
    DSS_OFFSET(v4l2_queryctrl, step);
    DSS_OFFSET(v4l2_queryctrl, default_value);
    DSS_OFFSET(v4l2_queryctrl, flags);

    DSS_SIZE(v4l2_querymenu);
    DSS_OFFSET(v4l2_querymenu, id);
    DSS_OFFSET(v4l2_querymenu, index);
    DSS_OFFSET(v4l2_querymenu, name);
    DSS_OFFSET(v4l2_querymenu, value);

    DSS_SIZE(v4l2_control);
    DSS_OFFSET(v4l2_control, id);
    DSS_OFFSET(v4l2_control, value);

    DSS_SIZE(uvc_xu_control_query);
    DSS_OFFSET(uvc_xu_control_query, unit);
    DSS_OFFSET(uvc_xu_control_query, selector);
    DSS_OFFSET(uvc_xu_control_query, query);
    DSS_OFFSET(uvc_xu_control_query, size);
    DSS_OFFSET(uvc_xu_control_query, data);
    return 0;
}
