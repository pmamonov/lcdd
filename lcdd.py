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

	def glcd_img(self, im, _x = 0, _y = 0, thresh = 0.5):
		for y in xrange(min(im.shape[0], 64)):
			for x in xrange(min(im.shape[1], 128)):
				if im[y, x, :3].sum() * 1. / (3 * 0xff) < thresh:
					ok = 0
					while not ok:
						try:
							self.glcd_pixel(_x + x,_y + y)
							ok = 1
						except:
							pass

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

def checker(lcd):
	for i in xrange(64/8):
		for j in xrange(128/8):
			if (i%2 == 0 and j%2 == 0 or i%2==1 and j%2 == 1):
				for k in xrange(8):
					for l in xrange(8):
						ok=0
						while not ok:
							try:
								lcd.glcd_pixel(8*j+k,8*i+l)
								ok=1
							except:
								pass

if __name__ == "__main__":
	lcd = lcdd()
	msg = sys.stdin.read().strip()
	lcd.set_msg(msg)
	print msg
