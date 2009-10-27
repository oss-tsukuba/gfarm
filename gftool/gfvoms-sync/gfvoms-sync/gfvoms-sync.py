#!/usr/bin/env python
# -*- encoding:utf-8 -*-

###############################################################
# gfvoms-sync
#
# Sync voms infos and gfarm
# Using gfvoms-list and gfvoms-update,
# synchronize voms's membership infos and that of gfarm.
#
# Copyright (c) 2009 National Institute of Informatics in Japan.
# All rights reserved.
##############################################################

import commands, getopt, sys, os, re
from hostid import hostids

tmpdir = "/tmp"
vomslist_tmpfile = "%s/vomslist.tmp"%(tmpdir)
options = {}

def usage(e):
	if e != None:
		print e
		e = ""
	usage_str ="""Usage:
    gfvoms-sync -s -V vomsID -v vo [-g voms-membership-path] [-a hostids] [-h]
    gfvoms-sync -d -V vomsID -v vo [-g voms-membership-path] [-a hostids] [-h]

    options:
        -s                  : Synchronization mode.
        -d                  : Deletion mode.
        -V voms             : ID of VOMS Admin Server
        -v vo               : Name of VO to get membership infos.[no default]
        -g membership-path  : Path of voms membership info file.[no default]
        -a vomsids          : Path of file of id and host.[default: '%s']
        -h                  : Show this Usage.\n"""
	print usage_str%(hostids)
	sys.exit(e)

def check_options():
	if (not options.has_key("sync")) and (not options.has_key("del")):
		usage("No Operation is found. You must set '-s' or '-d' option.")
	if (options.has_key("sync")) and options.has_key("del"):
		usage("Duplicate Operations. " +
		      "Option '-s' and '-d' cannot set in same time.")

	if not options.has_key("vomsid"):
		usage("No vomsID Specified!")
	m = re.compile("^[a-zA-Z0-9_-]*")
	if m.match(options["vomsid"]).group() != options["vomsid"]:
		sys.exit("The chars which can use for vomsID are [a-zA-Z0-9_-].")

	if not options.has_key("vo"):
		usage("No vo Specified!")

	if not options.has_key("hostids"):
		options["hostids"] = hostids

def parse_options():
	try:
		opts, args = getopt.getopt(sys.argv[1:], "sdg:a:V:v:nh")

		for key, val in opts:
			if key == "-V":
				options["vomsid"] = val
			elif key == "-v":
				options["vo"] = val
			elif key == "-g":
				options["vomslist"] = val
			elif key == "-a":
				options["hostids"] = val
			elif key == "-s":
				options["sync"] = ""
			elif key == "-d":
				options["del"] = ""
			elif key == "-n":
				options["noupd"] = ""
			elif key == "-h":
				usage(None)
		check_options()

		if len(args) != 0:
			usage("Invalid Argument is set!")

	except getopt.GetoptError, e:
		sys.exit("Error parsing command line arguments:%s"%(e))

def getGfvomsListOptions():
	ret= "-a %s -V %s -v %s -m %s"%(
	  options["hostids"], options["vomsid"], options["vo"], vomslist_tmpfile)
	return ret

def getGfvomsUpdOptions():
	ret= "-V %s -v %s"%(options["vomsid"], options["vo"])
	if options.has_key("del"):
		ret += " -d"
	else:
		ret += " -m %s"%(options["vomslist"])
	if options.has_key("noupd"):
		ret += " -n"
	return ret

def main():
	istmpfile = True
	try:
		# Init
		parse_options()

		# check gfvoms-list and gfvoms-update
		status,gfvoms_list = commands.getstatusoutput("which gfvoms-list")
		if status != 0:
			gfvoms_list = "./gfvoms-list"
		if not os.path.isfile(gfvoms_list):
			sys.exit("Cannot find command 'gfvoms-list'")
		status, gfvoms_update = commands.getstatusoutput(
		                              "which gfvoms-update")
		if status != 0:
			gfvoms_update = "./gfvoms-update"
		if not os.path.isfile(gfvoms_update):
			sys.exit("Cannot find command 'gfvoms-update'")
		if options.has_key("vomslist"):
			istmpfile = False

		# voms-list
		if not options.has_key("del"):
			if not options.has_key("vomslist"):
				if not os.path.exists(tmpdir):
					os.makedirs(tmpdir)
				print "gfvoms-list started..."
				status = os.system("%s %s"%(
				            gfvoms_list, getGfvomsListOptions()))
				if status != 0:
					raise Exception("gfvoms-list failed!")
				options["vomslist"] = vomslist_tmpfile
		# voms-update
		print "gfvoms-update started..."
		status = os.system("%s %s"%(gfvoms_update, getGfvomsUpdOptions()))
		if status != 0:
			raise Exception("gfvoms-update failed!")

		if not options.has_key("del"):
			print "Synchronization Finished Successfully!"
			if istmpfile and os.path.isfile(options["vomslist"]):
				os.remove(options["vomslist"])
		else:
			print "Deletion Finished Successfully!"

	except Exception, e:
		sys.exit("Error:%s"%(e))

if __name__ == "__main__":
	main()
