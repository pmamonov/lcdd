#!/usr/bin/python

import sys, usb, struct
from time import sleep
from math import log

usb_desc = [0x16c0, 0x05dc, "piton", "lcdd"]

def get_device(idVendor, idProduct, Manufacturer, Product):
	mydevh = None
	for bus in usb.busses():
		for dev in bus.devices:
		  if dev.idVendor == idVendor and dev.idProduct == idProduct:
		    devh = dev.open()
#        if devh.getString(dev.iManufacturer, len(Manufacturer)) == Manufacturer and  devh.getString(dev.iProduct, len(Product)) == Product:
		    mydevh = devh
	if not mydevh:
		raise NameError,"Device not found"
	return mydevh

class lcdd:
	def __init__(self):
		self.dev=get_device(*usb_desc)

	def set_del(self, ms):
		rq = usb.TYPE_VENDOR | usb.RECIP_DEVICE | usb.ENDPOINT_IN
		self.dev.controlMsg(rq, 0, 0, ms);

	def set_msg(self, msg):
		rq = usb.TYPE_VENDOR | usb.RECIP_DEVICE | usb.ENDPOINT_IN
		self.dev.controlMsg(rq, 1, 0, 0, len(msg));
		for i in xrange(len(msg)):
			self.dev.controlMsg(rq, 1, 0, ord(msg[i]), i);

if __name__ == "__main__":
	lcd = lcdd()
	msg = sys.stdin.read().strip()
	lcd.set_msg(msg)
	print msg
