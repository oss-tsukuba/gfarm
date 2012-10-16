import sys
import time
import subprocess
import traceback
import logging
from ctypes import *
from time import ctime
import io

descriptors = []

logging.basicConfig(level=logging.ERROR, format="%(asctime)s - %(name)s - %(levelname)s\t Thread-%(thread)d - %(message)s")
logging.debug('starting up')

last_update = 0
cur_time = 0
stats = {}
last_val = {}

MAX_UPDATE_TIME = 15
BYTES_PER_SECTOR = 512

# 5 GB
MIN_DISK_SIZE = 5242880
DEVICES = ''
IGNORE_DEV = 'dm-|loop|drbd'

PARTITIONS = []

class gfarm_iostat_head(Structure):
	_fields_ = [("s_magic",c_uint),
		("s_nitem",c_uint),
		("s_row",c_uint),
		("s_rowcur",c_uint),
		("s_rowmax",c_uint),
		("s_item_size",c_uint),
		("s_start_sec",c_ulonglong),
		("s_update_sec",c_ulonglong),
		("s_item_off",c_ulonglong),
		("s_name",c_char * 32)]

	def __str__(self):
		s = 'magic=' + repr(self.s_magic)
		s += ' nitem=' + repr(self.s_nitem)
		s += ' row=' + repr(self.s_row)
		s += ' rowcur=' + repr(self.s_rowcur)
		s += ' rowmax=' + repr(self.s_rowmax)
		s += ' item_size=' + repr(self.s_item_size)
		s += ' start_sec=' + repr(self.s_start_sec)
		s += ' update_sec=' + repr(self.s_update_sec)
		s += ' item_off=' + repr(self.s_item_off)
		s += ' name=' + repr(self.s_name)
		s += ' sec=' + ctime(self.s_start_sec)
		s += ' - ' + ctime(self.s_update_sec)
		return s

class gfarm_iostat_spec(Structure):
	_fields_ = [("s_name",c_char * 31),
		("s_type",c_int8)]
	def __str__(self):
		return self.s_name + ' ' + repr(self.s_type)

class gfarm_iostat_items(Structure):
	_fields_ = [("s_vals",c_ulonglong)]
	def __str__(self):
		return repr(self.s_vals)
	def diff(self, new):
		return  new.s_vals - self.svals

def sread(fd,cobj,size):
	memmove(pointer(cobj),c_char_p(fd.read(size)),size)

class gfarm_iostat:
	def __init__(self, infile):
		self.file = ''
		try:
			fd = open(infile, "rb")
		except:
			logging.error('file open error')
			return
		try:
			head = gfarm_iostat_head()
			sread(fd, head, sizeof(head))
			specs = gfarm_iostat_spec * head.s_nitem
			aspec = specs()
			sread(fd, aspec, sizeof(specs))
			items = gfarm_iostat_items * ((head.s_nitem + 1) *
				head.s_rowmax)
			aitems = items()
			sread(fd, aitems, sizeof(items))
		except:
			logging.execption('sread')
			fd.close()
			return
		self.file = infile
		self.ahead = head
		self.aspec = aspec
		self.aitems = aitems
		fd.close()
	def isvalid(self):
		if self.file == '' :
			return False
		return True
	def head(self):
		if self.file == '' :
			return
		return self.ahead
	def nitem(self):
		if self.file == '' :
			return
		return self.ahead.s_nitem
	def nraw(self):
		if self.file == '' :
			return
		return self.ahead.s_rowmax
	def spec(self):
		if self.file == '' :
			return
		return self.aspec
	def items(self):
		if self.file == '' :
			return
		return self.items
	def newitems(self):
		return  gfarm_iostat_items * self.ahead.s_nitem
	def newvals(self):
		t = []
		t += map((lambda x : 0), range(0, self.ahead.s_nitem))
		return t
	def getrow(self, row):
		if self.file == '' :
			return
		if self.ahead.s_row <= row :
			return
		k = row * (self.ahead.s_nitem + 1)
		return self.aitems[k], self.aitems[k+1:k+1+self.ahead.s_nitem]
	def __str__(self):
		if self.file == '' :
			return ''
		s = str(self.ahead)
		s+= '\n'
		for spec in self.aspec:
			s += str(spec) + ' '
		s+= '\n'
		for j in range(0, self.ahead.s_rowmax):
			(valid, items) = self.getrow(j)
			s += str(valid) + ':'
			delm = ''
			for item in items:
				s += delm + str(item)
				delm = ','
			s+= '\n'
		return s

	def sumup(self, inx):
		t = self.newvals()
		for j in range(inx, self.ahead.s_rowmax):
			(valid, items) = self.getrow(j)
			t = map((lambda x,y: x+y.s_vals), t, items)
		return t
	def diffitems(self, old, new):
		t = []
		t += map((lambda o,n: n.s_vals - o.s_vals), old, new)
		return t

	def diff(self, new):
		if new.ahead.s_start_sec > self.ahead.s_start_sec:
			return new.sumup(0)
		t = self.newvals()
		upto = new.ahead.s_rowmax
		if new.ahead.s_rowmax > self.ahead.s_rowmax:
			t = new.sumup(self.ahead.s_rowmax)
			upto = self.ahead.s_rowmax
		for i in range(0, upto):
			(valid, o) = self.getrow(i)
			(valid, n) = new.getrow(i)
			v = self.diffitems(o, n)
			t = map((lambda x,y: x + y), t, v)
		return t

if __name__ == '__main__':
	from optparse import OptionParser
	import os

	logging.debug('running from cmd line')
	parser = OptionParser()
	parser.add_option('-i', '--infile', dest='infile', default='', help='infile to explicitly check')
	parser.add_option('-s', '--step', dest='step', default=1, help='interval step to check')

	(options, args) = parser.parse_args()

	old = gfarm_iostat(options.infile)
	for i in range(1, 100):
		stat = gfarm_iostat(options.infile)
		if stat.isvalid() :
			print stat
			print "sumup=", stat.sumup(0)
			print "diff=", old.diff(stat)
			old = stat
			time.sleep(float(options.step))

