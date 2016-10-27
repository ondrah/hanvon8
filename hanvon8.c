#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <asm/unaligned.h>

#define DRIVER_VERSION "0.1"
#define DRIVER_AUTHOR "Ondra Havel <ondra.havel@gmail.com>"
#define DRIVER_DESC "USB Hanvon8 tablet driver"
#define DRIVER_LICENSE "GPL"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

#define USB_VENDOR_ID_HANVON	0x0b57
#define USB_PRODUCT_ID_NXS1310	0x8030

#define USB_AM_PACKET_LEN   8

#define AM_MAX_PRESSURE   0x400

struct hanvon {
	unsigned char *data;
	dma_addr_t data_dma;
	struct input_dev *dev;
	struct usb_device *usbdev;
	struct urb *irq;
	char phys[32];
};

static inline void handle_default(struct hanvon *hanvon)
{
	unsigned char *data = hanvon->data;
	struct input_dev *dev = hanvon->dev;

#define AM_MAX_ABS_X   0x27de
#define AM_MAX_ABS_Y   0x1cfe

	input_report_key(dev, BTN_LEFT, data[1] & 0x01); /* pen touches the surface */
	input_report_key(dev, BTN_RIGHT, data[1] & 0x02); /* stylus button pressed (right click) */
	input_report_abs(dev, ABS_X, get_unaligned_le16(&data[2]));
	input_report_abs(dev, ABS_Y, get_unaligned_le16(&data[4]));
	input_report_abs(dev, ABS_PRESSURE, get_unaligned_le16(&data[6]));
}

static void hanvon_irq(struct urb *urb)
{
	struct hanvon *hanvon = urb->context;
	int retval;

	switch (urb->status) {
		case 0:
			/* success */
			handle_default(hanvon);
			break;
		case -ECONNRESET:
		case -ENOENT:
		case -ESHUTDOWN:
			/* this urb is terminated, clean up */
			printk("%s - urb shutting down with status: %d\n", __func__, urb->status);
			return;
		default:
			printk("%s - nonzero urb status received: %d\n", __func__, urb->status);
				break;
	}

	input_sync(hanvon->dev);

	retval = usb_submit_urb (urb, GFP_ATOMIC);
	if (retval)
		printk("%s - usb_submit_urb failed with result %d\n", __func__, retval);
}

static struct usb_device_id hanvon_ids[] = {
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_NXS1310) },
	{}
};

MODULE_DEVICE_TABLE(usb, hanvon_ids);

static int hanvon_open(struct input_dev *dev)
{
	struct hanvon *hanvon = input_get_drvdata(dev);

	hanvon->irq->dev = hanvon->usbdev;
	if (usb_submit_urb(hanvon->irq, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void hanvon_close(struct input_dev *dev)
{
	struct hanvon *hanvon = input_get_drvdata(dev);

	usb_kill_urb(hanvon->irq);
}

static int hanvon_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *endpoint;
	struct hanvon *hanvon;
	struct input_dev *input_dev;
	int error = -ENOMEM;

	hanvon = kzalloc(sizeof(struct hanvon), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!hanvon || !input_dev)
		goto fail1;

	hanvon->data = (unsigned char *)usb_alloc_coherent(dev, USB_AM_PACKET_LEN, GFP_KERNEL, &hanvon->data_dma);
	if (!hanvon->data)
		goto fail1;

	hanvon->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!hanvon->irq)
		goto fail2;

	hanvon->usbdev = dev;
	hanvon->dev = input_dev;

	usb_make_path(dev, hanvon->phys, sizeof(hanvon->phys));
	strlcat(hanvon->phys, "/input0", sizeof(hanvon->phys));

	input_dev->name = "Hanvon tablet";
	input_dev->phys = hanvon->phys;
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

	input_set_drvdata(input_dev, hanvon);

	input_dev->open = hanvon_open;
	input_dev->close = hanvon_close;

	input_dev->evbit[0] |= BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) | BIT_MASK(EV_REL);
	input_dev->keybit[BIT_WORD(BTN_DIGI)] |= BIT_MASK(BTN_TOOL_PEN) | BIT_MASK(BTN_TOUCH);
	input_dev->keybit[BIT_WORD(BTN_LEFT)] |= BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_MIDDLE);

	input_set_abs_params(input_dev, ABS_X, 0, AM_MAX_ABS_X, 4, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, AM_MAX_ABS_Y, 4, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, AM_MAX_PRESSURE, 0, 0);

	endpoint = &intf->cur_altsetting->endpoint[0].desc;

	usb_fill_int_urb(hanvon->irq, dev,
			usb_rcvintpipe(dev, endpoint->bEndpointAddress),
			hanvon->data, USB_AM_PACKET_LEN,
			hanvon_irq, hanvon, endpoint->bInterval);
	hanvon->irq->transfer_dma = hanvon->data_dma;
	hanvon->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	error = input_register_device(hanvon->dev);
	if (error)
		goto fail3;

	usb_set_intfdata(intf, hanvon);
	return 0;

fail3:   usb_free_urb(hanvon->irq);
fail2:   usb_free_coherent(dev, USB_AM_PACKET_LEN, hanvon->data, hanvon->data_dma);
fail1:   input_free_device(input_dev);
	kfree(hanvon);
	return error;
}

static void hanvon_disconnect(struct usb_interface *intf)
{
	struct hanvon *hanvon = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);
	if (hanvon) {
		usb_kill_urb(hanvon->irq);
		input_unregister_device(hanvon->dev);
		usb_free_urb(hanvon->irq);
		usb_free_coherent(interface_to_usbdev(intf), USB_AM_PACKET_LEN, hanvon->data, hanvon->data_dma);
		kfree(hanvon);
	}
}

static struct usb_driver hanvon_driver = {
	.name = "hanvon",
	.probe = hanvon_probe,
	.disconnect = hanvon_disconnect,
	.id_table =   hanvon_ids,
};

static int __init hanvon_init(void)
{
	int rv;

	if((rv = usb_register(&hanvon_driver)) != 0)
		return rv;

	printk(DRIVER_DESC " " DRIVER_VERSION "\n");

	return 0;
}

static void __exit hanvon_exit(void)
{
	usb_deregister(&hanvon_driver);
}

module_init(hanvon_init);
module_exit(hanvon_exit);
