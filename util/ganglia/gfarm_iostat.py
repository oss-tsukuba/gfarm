import sys
import time
import subprocess
import traceback
import logging
from ctypes import *
from time import ctime
import io
import os
import fileinput

GFARM_IOSTAT_MAGIC = 0x53544132

logging.basicConfig(level=logging.WARNING, format="%(asctime)s - %(name)s - %(levelname)s\t Thread-%(thread)d - %(message)s")
#logging.basicConfig(level=logging.DEBUG, format="%(asctime)s - %(name)s - %(levelname)s\t Thread-%(thread)d - %(message)s")
logging.debug('starting up')

descriptors = []
last_update = 0
cur_time = 0
gfarm_labels = []
gfarm_0items = []
iostat_label = ''

class gfarm_iostat_head(Structure):
	_fields_ = [("s_magic",c_uint),
		("s_nitem",c_uint),
		("s_row",c_uint),
		("s_rowcur",c_uint),
		("s_rowmax",c_uint),
		("s_item_size",c_uint),
		("s_ncolumn",c_uint),
		("s_dummy",c_uint),
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
		s += ' ncolumn=' + repr(self.s_ncolumn)
		s += ' dummy=' + repr(self.s_dummy)
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
		return new.s_vals - self.svals

def sread(fd,cobj,size):
	memmove(pointer(cobj),c_char_p(fd.read(size)),size)

class gfarm_iostat:
	def __init__(self, infile):
		global gfarm_labels
		global gfarm_0items
		self.file = ''
		try:
			fd = open(infile, "rb")
		except:
			logging.error('open ' + infile +
				str(sys.exc_type) + str(sys.exc_value))
			raise
			return
		try:
			head = gfarm_iostat_head()
			sread(fd, head, sizeof(head))
			if head.s_magic != GFARM_IOSTAT_MAGIC :
				logging.error('Invalid magic '
					+ repr(head.s_magic))
				raise
			specs = gfarm_iostat_spec * head.s_nitem
			aspec = specs()
			sread(fd, aspec, sizeof(specs))

			xn = head.s_ncolumn * head.s_rowmax
			items = gfarm_iostat_items * xn
			aitems = items()
			fd.seek(head.s_item_off)
			sread(fd, aitems, sizeof(items))
		except:
			logging.error('read ' + infile +
				str(sys.exc_type) + str(sys.exc_value))
			fd.close()
			raise
			return
		t = []
		t += map((lambda x : x.s_name), aspec)
		self.file = infile
		self.ahead = head
		self.aspec = aspec
		self.names = t
		gfarm_labels = t
		self.aitems = aitems
		if len(gfarm_0items) == 0 :
			gfarm_0items += map((lambda x : 0), range(0, self.ahead.s_nitem))
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
	def ncolumn(self):
		if self.file == '' :
			return
		return self.ahead.s_ncolumn
	def nraw(self):
		if self.file == '' :
			return
		return self.ahead.s_row
	def spec(self):
		if self.file == '' :
			return
		return self.aspec
	def getnames(self):
		if self.file == '' :
			return
		return self.names
	def items(self):
		if self.file == '' :
			return
		return self.items
	def newitems(self):
		return gfarm_iostat_items * self.ahead.s_nitem
	def newvals(self):
		t = []
		t += map((lambda x : 0), range(0, self.ahead.s_nitem))
		return t
	def getrow(self, row):
		if self.file == '' :
			return
		if self.ahead.s_row <= row :
			return
		k = row * self.ahead.s_ncolumn
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

class dir_iostat(gfarm_iostat):
	def __init__(self, indir):
		self.ents = {}
		for ent in os.listdir(indir):
			st = gfarm_iostat(indir + '/' + ent)
			self.ents[ent] = st
	def sumup(self, inx):
		total = gfarm_0items
		for file, st in self.ents.iteritems() :
			stat = st.sumup(0)
			total = map((lambda x,y: x+y), total, stat)
		return (total)
	def diff(self, news):
		total = gfarm_0items
		for file, new in news.ents.iteritems() :
			if self.ents.has_key(file) :
				stat = self.ents[file].diff(new)
			else :
				stat = new.sumup(0)
			total = map((lambda x,y: x+y), total, stat)
		return (total)

gfarm_counterdir = ''
gfarm_files = []
gfarm_filestats = []


def get_filesstats(files):
	filestats = {}
	for file in files :
		filestats[file] = dir_iostat(gfarm_counterdir + '/' + file)
	return filestats

def calc_total(old, stat) :
	count = {}
	oval = [old[k] for k in gfarm_labels]
	tot = map((lambda t,n : t+n), oval, stat)
	count.update(map((lambda k,v : (k, v)), gfarm_labels, tot))
	return count
	
def get_filesdiff(olds, news, lastcounts) :
	total = gfarm_0items
	diffs = {}
	counts = {}

	for file, new in news.items() :
		diff = {}
		if file in olds :
			stat = olds[file].diff(new)
		else :
			stat = new.sumup(0)
		diff.update(map((lambda k,v : (k, v)), gfarm_labels, stat))
		diffs[file] = diff
		total = map((lambda x,y: x+y), total, stat)

		if file in lastcounts :
			last = lastcounts[file]
			counts[file] = calc_total(last, stat)
		else :
			counts[file] = diff
		

	return (total, diffs, counts)

gfarm_diffs = {}
gfarm_counts = {}
ntimes = 0

def get_stats():
	global gfarm_filestats, gfarm_diffs, gfarm_counts

	label = 'iostat-' + gfarm_server + iostat_label
	diff = {}
	new = get_filesstats(gfarm_files)
	(total, gfarm_diffs, newcount) = get_filesdiff(gfarm_filestats, new, 
						gfarm_counts)
	diff.update(map((lambda k,v : (k, v)), gfarm_labels, total))
	gfarm_diffs[label] = diff
	if label in gfarm_counts :
		newcount[label] = calc_total(gfarm_counts[label], total)
	else :
		newcount[label] = diff
	gfarm_counts = newcount
	gfarm_filestats = new

def get_stat(name):
	global gfarm_counts, ntimes

	if ntimes % len(descriptors) == 0 :
		get_stats()

	ntimes += 1

	sep = name.rfind('_')
	file = name[:sep]
	label = name[sep + 1:]
	try:
		count = gfarm_counts[file]
		return int(count[label])
	except:
		logging.warning('failed to fetch ' + name)
		return 0

def list_files():
	files = []

	for ent in os.listdir(gfarm_counterdir):
		if ent.startswith('iostat-' + gfarm_server) :
			if ent not in files :
				files.append(ent)
	return files

def metric_init(params):
	global descriptors, gfarm_0items, gfarm_files, iostat_label
	global gfarm_server, gfarm_counterdir, gfarm_filestats

	label = params.get('iostat_label')
	group = 'gfarm'
	if len(label) > 0 :
		iostat_label = '-' + label
		group = label
	gfarm_server = params.get('gfarm-server')
	counterdir = params.get('iostat_counterdir')

	logging.debug('init: ' + str(params))

	if os.path.exists(counterdir) :
		gfarm_counterdir = counterdir
	else :
		logging.error('init: ' + counterdir)
		raise
		return

	time_max = 60

	if gfarm_server == 'gfmd' :
		descriptions = dict(
		ntran = {
			'units': 'transactions',
			'description': 'The number of transactions received'},
		)
	else :
		descriptions = dict(
		rcount = {
			'units': 'counts',
			'description': 'The number of read transactions received'},
		wcount = {
			'units': 'counts',
			'description': 'The number of write transactions received'},
		rbytes = {
			'units': 'bytes',
			'description': 'The total bytes read '},
		wbytes = {
			'units': 'bytes',
			'description': 'The total bytes wrote '},
		)

	logging.debug('gfarm_counterdir: ' + gfarm_counterdir)
	gfarm_files = list_files()
	if len(gfarm_files) == 0 :
		logging.error('no entries: ' + gfarm_counterdir)
		return

	try :
		gfarm_filestats = get_filesstats(gfarm_files)
	except:
		logging.warning('list error: ' + gfarm_counterdir)

	files = gfarm_files[:]
	if len(files) > 1 :
		files.append('iostat-' + gfarm_server + iostat_label)

	for label in descriptions:
		for name in files :
			d = {
				'name': name + '_' + label,
				'call_back': get_stat,
				'time_max': time_max,
				'value_type': 'uint',
				'units': '',
				'slope': 'positive',
				'format': '%u',
				'description': label,
				'groups': group
				}

			# Apply metric customizations from descriptions
			d.update(descriptions[label])

			descriptors.append(d)

	logging.debug('descriptors: ' + str(descriptors))

	return descriptors

def metric_cleanup():
	logging.shutdown()
	# pass


if __name__ == '__main__':
	from optparse import OptionParser

	logging.debug('running from cmd line')
	parser = OptionParser()
	parser.add_option('-d', '--counterdir', dest='iostat_counterdir', default='/var/gfarm-stat', help='gfarm counter directory path')
	parser.add_option('-t', '--target', dest='server', default='gfmd', help='gfmd or gfsd')
	parser.add_option('-l', '--label', dest='label', default='', help='cluster label')
	parser.add_option('-s', '--step', dest='step', default=1, help='interval step to check')
	parser.add_option('-b', '--gmetric-bin', dest='gmetric_bin', default='/usr/bin/gmetric', help='path to gmetric binary')
	parser.add_option('-c', '--gmond-conf', dest='gmond_conf', default='/etc/ganglia/gmond.conf', help='path to gmond.conf')
	parser.add_option('-g', '--gmetric', dest='gmetric', action='store_true', default=False, help='submit via gmetric')


	(options, args) = parser.parse_args()

	metric_init({
		'iostat_counterdir': options.iostat_counterdir,
		'iostat_label': options.label,
		'gfarm-server': options.server,
	})

	while True:
		for d in descriptors:
			v = d['call_back'](d['name'])
			print ' %s: %s %s [%s]' % (d['name'], v, d['units'], d['description'])

			if options.gmetric:
				if d['value_type'] == 'uint':
					value_type = 'uint32'
				else:
					value_type = d['value_type']

				cmd = "%s --conf=%s --value='%s' --units='%s' --type='%s' --name='%s' --slope='%s'" % \
					(options.gmetric_bin, options.gmond_conf, v, d['units'], value_type, d['name'], d['slope'])
				os.system(cmd)

		print 'Sleeping 5 seconds'
		time.sleep(5)
