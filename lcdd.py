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
		# put zero after last symbol
		self.dev.controlMsg(rq, 1, 0, 0, len(msg));
		for i in xrange(len(msg)):
			self.dev.controlMsg(rq, 1, 0, ord(msg[i]), i);

	def glcd_clr(self):
		rq = usb.TYPE_VENDOR | usb.RECIP_DEVICE | usb.ENDPOINT_IN
		self.dev.controlMsg(rq, 3, 0);

	def glcd_pixel(self, x, y):
		rq = usb.TYPE_VENDOR | usb.RECIP_DEVICE | usb.ENDPOINT_IN
		self.dev.controlMsg(rq, 2, 0, x | (y << 8));

def fill(c, i, l):
	return i * c + (l - i) * ' '

def heartbeet(d = 0.1, cup = '>', cdown = '<'):
	lcd = lcdd()
	lcd.set_del(0)
	while 1:
		for i in xrange(11):
			try:
				lcd.set_msg(fill(cup, i, 10))
			except:
				pass
			sleep(d)
		for i in xrange(9,0,-1):
			try:
				lcd.set_msg(fill(cdown, i, 10))
			except:
				pass
			sleep(d)

if __name__ == "__main__":
	lcd = lcdd()
	msg = sys.stdin.read().strip()
	lcd.set_msg(msg)
	print msg
