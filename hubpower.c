/* hubpower -- control the power settings for a USB hub
 *
 * To build: gcc -o hubpower hubpower.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <asm/byteorder.h>
#include <linux/usbdevice_fs.h>
#include <linux/usb/ch9.h>

#define USB_HUB_TIMEOUT     5000    /* milliseconds */
#define USB_PORT_FEAT_POWER 8

#define USB_DT_HUB      (USB_TYPE_CLASS | 0x09)
#define USB_DT_HUB_SIZE     7

struct usb_hub_descriptor {
    __u8  bDescLength;
    __u8  bDescriptorType;
    __u8  bNbrPorts;
    __le16 wHubCharacteristics;
    __u8  bPwrOn2PwrGood;
    __u8  bHubContrCurrent;
} __attribute__ ((packed));

struct usb_port_status {
    __le16 wPortStatus;
    __le16 wPortChange;
} __attribute__ ((packed));

#define USB_PORT_STAT_CONNECTION    0x0001
#define USB_PORT_STAT_ENABLE        0x0002
#define USB_PORT_STAT_SUSPEND       0x0004
#define USB_PORT_STAT_OVERCURRENT   0x0008
#define USB_PORT_STAT_RESET     0x0010
#define USB_PORT_STAT_L1        0x0020
/* bits 6 to 7 are reserved */
#define USB_PORT_STAT_POWER     0x0100
#define USB_PORT_STAT_LOW_SPEED     0x0200
#define USB_PORT_STAT_HIGH_SPEED        0x0400
#define USB_PORT_STAT_TEST              0x0800
#define USB_PORT_STAT_INDICATOR         0x1000
#define USB_PORT_STAT_POWER_3       0x0200  /* USB 3.0 */


int fd;     /* Hub device file */
int usb_level;


void usage(void)
{
    fprintf(stderr, "Usage:"
        "\thubpower busnum:devnum power {portnum (on|off)} ...\n"
        "\thubpower busnum:devnum status\n"
        "\thubpower busnum:devnum bind\n"
        );
    exit(1);
}

void port_status(int portnum)
{
    struct usbdevfs_ctrltransfer ctrl;
    struct usb_port_status pstat;
    int rc;

    ctrl.bRequestType = USB_DIR_IN | USB_TYPE_CLASS |
            USB_RECIP_OTHER;
    ctrl.bRequest = USB_REQ_GET_STATUS;
    ctrl.wValue = 0;
    ctrl.wIndex = portnum;
    ctrl.wLength = sizeof(pstat);
    ctrl.timeout = USB_HUB_TIMEOUT;
    ctrl.data = &pstat;
    rc = ioctl(fd, USBDEVFS_CONTROL, &ctrl);
    if (rc == -1) {
        fprintf(stderr, "Error in ioctl "
            "(get port %d status): %s\n",
            portnum, strerror(errno));
        return;
    }

    printf("Port %2d status: %04x ", portnum, pstat.wPortStatus);

    if (usb_level <= 2) {
        if (pstat.wPortStatus & USB_PORT_STAT_INDICATOR)
            printf(" Indicator");
        if (pstat.wPortStatus & USB_PORT_STAT_TEST)
            printf(" Test-Mode");
        if (pstat.wPortStatus & USB_PORT_STAT_HIGH_SPEED)
            printf(" High-Speed");
        if (pstat.wPortStatus & USB_PORT_STAT_LOW_SPEED)
            printf(" Low-Speed");
        if (pstat.wPortStatus & USB_PORT_STAT_POWER)
            printf(" Power-On");
        else
            printf(" Power-Off");
    } else if (usb_level == 3) {
        if (pstat.wPortStatus & USB_PORT_STAT_POWER_3)
            printf(" Power-On");
        else
            printf(" Power-Off");
    }

    if (pstat.wPortStatus & USB_PORT_STAT_RESET)
        printf(" Resetting");
    if (pstat.wPortStatus & USB_PORT_STAT_OVERCURRENT)
        printf(" Overcurrent");
    if (pstat.wPortStatus & USB_PORT_STAT_SUSPEND)
        printf(" Suspended");
    if (pstat.wPortStatus & USB_PORT_STAT_ENABLE)
        printf(" Enabled");
    if (pstat.wPortStatus & USB_PORT_STAT_CONNECTION)
        printf(" Connected");

    printf("\n");
}

int main(int argc, char **argv)
{
    int busnum, devnum, numports;
    enum {DO_POWER, DO_STATUS, DO_BIND} action;
    char fname1[40], fname2[40];
    int rc;
    int portnum;
    struct usb_device_descriptor dev_descr;
    struct usb_hub_descriptor hub_descr;
    struct usbdevfs_ctrltransfer ctrl;
    struct usbdevfs_ioctl usb_ioctl;
    int bus_endian;

    if (argc < 3)
        usage();
    if (sscanf(argv[1], "%d:%d", &busnum, &devnum) != 2 ||
            busnum <= 0 || busnum > 255 ||
            devnum <= 0 || devnum > 255)
        usage();

    if (strcmp(argv[2], "power") == 0) {
        action = DO_POWER;
        if ((argc - 3) % 2 != 0)
            usage();
    } else if (strcmp(argv[2], "status") == 0) {
        action = DO_STATUS;
        if (argc != 3)
            usage();
    } else if (strcmp(argv[2], "bind") == 0) {
        action = DO_BIND;
        if (argc != 3)
            usage();
    } else {
        usage();
    }

    sprintf(fname1, "/dev/bus/usb/%03d/%03d", busnum, devnum);
    sprintf(fname2, "/proc/bus/usb/%03d/%03d", busnum, devnum);

    bus_endian = 1;
    fd = open(fname1, O_RDWR);
    if (fd < 0) {
        int err1 = errno;

        bus_endian = 0;
        fd = open(fname2, O_RDWR);
        if (fd < 0) {
            fprintf(stderr, "Unable to open device file %s: %s\n",
                    fname1, strerror(err1));
            fprintf(stderr, "Unable to open device file %s: %s\n",
                    fname2, strerror(errno));
            return 1;
        }
    }

    rc = read(fd, &dev_descr, USB_DT_DEVICE_SIZE);
    if (rc != USB_DT_DEVICE_SIZE) {
        perror("Error reading device descriptor");
        return 1;
    }
    if (dev_descr.bDeviceClass != USB_CLASS_HUB) {
        fprintf(stderr, "Device %d:%d is not a hub\n",
                busnum, devnum);
        return 1;
    }
    if (bus_endian) {
        dev_descr.bcdUSB = __le16_to_cpu(dev_descr.bcdUSB);
    }
    usb_level = dev_descr.bcdUSB >> 8;

    ctrl.bRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_DEVICE;
    ctrl.bRequest = USB_REQ_GET_DESCRIPTOR;
    ctrl.wValue = USB_DT_HUB << 8;
    ctrl.wIndex = 0;
    ctrl.wLength = USB_DT_HUB_SIZE;
    ctrl.timeout = USB_HUB_TIMEOUT;
    ctrl.data = &hub_descr;
    rc = ioctl(fd, USBDEVFS_CONTROL, &ctrl);
    if (rc == -1) {
        perror("Error in ioctl (read hub descriptor)");
        return 1;
    }
    numports = hub_descr.bNbrPorts;

    if (action == DO_STATUS) {
        for (portnum = 1; portnum <= numports; ++portnum)
            port_status(portnum);
        return 0;
    }

    if (action == DO_BIND) {
        usb_ioctl.ifno = 0;
        usb_ioctl.ioctl_code = USBDEVFS_CONNECT;
        usb_ioctl.data = NULL;
        rc = ioctl(fd, USBDEVFS_IOCTL, &usb_ioctl);
        if (rc == -1) {
            perror("Error in ioctl (USBDEVFS_CONNECT)");
            return 1;
        }
        printf("Bind-driver request sent to the kernel\n");
        return 0;
    }

    if (action == DO_POWER) {
        int i;

        usb_ioctl.ifno = 0;
        usb_ioctl.ioctl_code = USBDEVFS_DISCONNECT;
        usb_ioctl.data = NULL;
        rc = ioctl(fd, USBDEVFS_IOCTL, &usb_ioctl);
        if (rc == -1 && errno != ENODATA) {
            perror("Error in ioctl (USBDEVFS_DISCONNECT)");
            return 1;
        }

        for (i = 3; i < argc; i += 2) {
            portnum = atoi(argv[i]);
            if (portnum < 1 || portnum > numports) {
                fprintf(stderr, "Invalid port number: %d\n",
                        portnum);
                continue;
            }

            if (strcmp(argv[i+1], "on") == 0)
                ctrl.bRequest = USB_REQ_SET_FEATURE;
            else if (strcmp(argv[i+1], "off") == 0)
                ctrl.bRequest = USB_REQ_CLEAR_FEATURE;
            else {
                fprintf(stderr, "Invalid port power level: %s\n)",
                        argv[i+1]);
                continue;
            }

            ctrl.bRequestType = USB_DIR_OUT | USB_TYPE_CLASS |
                    USB_RECIP_OTHER;
            ctrl.wValue = USB_PORT_FEAT_POWER;
            ctrl.wIndex = portnum;
            ctrl.wLength = 0;
            ctrl.timeout = USB_HUB_TIMEOUT;
            ctrl.data = NULL;
            rc = ioctl(fd, USBDEVFS_CONTROL, &ctrl);
            if (rc == -1) {
                fprintf(stderr, "Error in ioctl "
                    "(set/clear port %d feature): %s\n",
                    portnum, strerror(errno));
                continue;
            }

            port_status(portnum);
        }
    }
    return 0;
}

