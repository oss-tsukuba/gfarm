#!/usr/bin/env python
# -*- encoding: utf-8 -*-

######################################################################
# Copyright (c) 2009 National Institute of Informatics in Japan.
# All rights reserved.
#
# Apache 2.0 Licensed Products:
#  This product includes software listed below that were licensed under
#  the Apache License, Version 2.0 (the "License"); you may not use this file
#  except in compliance with the License.
#  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing,
#  software distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions
#  and limitations under the License.
#
#  VOMSAdmin, (C) 2006. Members of the EGEE Collaboration.
######################################################################

"""modules to use ZSI.
"""

nspace = "http://glite.org/wsdl/services/org.glite.security.voms"
encstyle = "http://schemas.xmlsoap.org/soap/encoding/"

import string, getopt, sys, os, os.path, types, re, urlparse
import ZSI
from ZSI import client

class ns0:
	targetNamespace = nspace
	
	class User_Def(ZSI.TCcompound.ComplexType, ZSI.schema.TypeDefinition):
		schema = nspace
		type = (schema, "User")
		def __init__(self, pname, ofwhat=(), attributes=None,
		             extend=False, restrict=False, **kw):
			ns = self.__class__.schema
			option = {"minOccurs":1, "maxOccurs":1, "nillable":True,
			          "typed":False, "encoded":kw.get("encoded")}
			TClist = [
				ZSI.TC.String(pname="CA", aname="_CA", **option),
				ZSI.TC.String(pname="CN", aname="_CN", **option),
				ZSI.TC.String(pname="DN", aname="_DN", **option),
				ZSI.TC.String(pname="certUri", aname="_certUri", **option),
				ZSI.TC.String(pname="mail", aname="_mail", **option)]
			self.attribute_typecode_dict = attributes or {}
			if extend: TClist += ofwhat
			if restrict: TClist = ofwhat
			ZSI.TCcompound.ComplexType.__init__(
			            self, None, TClist, pname=pname, inorder=0, **kw)
			class Holder:
				typecode = self
				def __init__(self):
					# pyclass
					self._CA = None
					self._CN = None
					self._DN = None
					self._certUri = None
					self._mail = None
					return
				def __repr__(self):
					return "%s, %s" % (self._DN, self._CA)
			Holder.__name__ = "User_Holder"
			self.pyclass = Holder
	
	class VOMSException_Def(ZSI.TCcompound.ComplexType,
	                               ZSI.schema.TypeDefinition):
		schema = nspace
		type = (schema, "VOMSException")
		def __init__(self, pname, ofwhat=(),
		             attributes=None, extend=False, restrict=False, **kw):
			ns = self.__class__.schema
			TClist = []
			self.attribute_typecode_dict = attributes or {}
			if extend: TClist += ofwhat
			if restrict: TClist = ofwhat
			ZSI.TCcompound.ComplexType.__init__(self, None, TClist,
			                                    pname=pname, inorder=0, **kw)
			class Holder:
				typecode = self
				def __init__(self):
					# pyclass
					return
			Holder.__name__ = "VOMSException_Holder"
			self.pyclass = Holder

class ns1:
	targetNamespace = "%s.service.admin"%(nspace)
	
	class ArrayOf_tns2_User_Def(ZSI.TC.Array, ZSI.schema.TypeDefinition):
		#complexType/complexContent base="SOAP-ENC:Array"
		schema = "%s.service.admin"%(nspace)
		type = (schema, "ArrayOf_tns2_User")
		def __init__(self, pname, ofwhat=(), extend=False,
		             restrict=False, attributes=None, **kw):
			ofwhat = ns0.User_Def(None, typed=False)
			atype = (nspace, u'User[]')
			ZSI.TCcompound.Array.__init__(self, atype, ofwhat, pname=pname,
			                              childnames='item', **kw)

	class ArrayOf_soapenc_string_Def(ZSI.TC.Array, ZSI.schema.TypeDefinition):
		#complexType/complexContent base="SOAP-ENC:Array"
		schema = "%s.service.admin"%(nspace)
		type = (schema, "ArrayOf_soapenc_string")
		def __init__(self, pname, ofwhat=(), extend=False,
		             restrict=False, attributes=None, **kw):
			ofwhat = ZSI.TC.String(None, typed=False)
			atype = (encstyle, u'string[]')
			ZSI.TCcompound.Array.__init__(self, atype, ofwhat, pname=pname,
			                              childnames='item', **kw)

class listUsersWithRoleRequest:
	def __init__(self):
		self._in0 = None
		self._in1 = None
		return
listUsersWithRoleRequest.typecode = ZSI.TCcompound.Struct(
	pname=("%s.service.admin"%(nspace),"listUsersWithRole"),
	ofwhat=[
		ZSI.TC.String(pname="in0", aname="_in0", typed=False, encoded=None,
		              minOccurs=1, maxOccurs=1, nillable=True),
		ZSI.TC.String(pname="in1", aname="_in1", typed=False, encoded=None,
		              minOccurs=1, maxOccurs=1, nillable=True)],
	pyclass=listUsersWithRoleRequest,
	encoded="%s.service.admin"%(nspace))

class listUsersWithRoleResponse:
	def __init__(self):
		self._listUsersWithRoleReturn = None
		return
listUsersWithRoleResponse.typecode = ZSI.TCcompound.Struct(
	pname=("%s.service.admin"%(nspace),"listUsersWithRoleResponse"),
	ofwhat=[
		ns1.ArrayOf_tns2_User_Def(
			pname="listUsersWithRoleReturn",
			aname="_listUsersWithRoleReturn",
			typed=False, encoded=None,
			minOccurs=1, maxOccurs=1, nillable=True)],
	pyclass=listUsersWithRoleResponse,
	encoded="%s.service.admin"%(nspace))

class listMembersRequest:
	def __init__(self):
		self._in0 = None
		return
listMembersRequest.typecode = ZSI.TCcompound.Struct(
	pname=("%s.service.admin"%(nspace),"listMembers"),
	ofwhat=[
		ZSI.TC.String(
			pname="in0", aname="_in0", typed=False, encoded=None,
			minOccurs=1, maxOccurs=1, nillable=True)],
	pyclass=listMembersRequest,
	encoded="%s.service.admin"%(nspace))

class listMembersResponse:
	def __init__(self):
		self._listMembersReturn = None
		return
listMembersResponse.typecode = ZSI.TCcompound.Struct(
	pname=("%s.service.admin"%(nspace),"listMembersResponse"),
	ofwhat=[
		ns1.ArrayOf_tns2_User_Def(
			pname="listMembersReturn", aname="_listMembersReturn",
			typed=False, encoded=None,
			minOccurs=1, maxOccurs=1, nillable=True)],
	pyclass=listMembersResponse,
	encoded="%s.service.admin"%(nspace))

class listRolesRequest:
	def __init__(self):
		self._in0 = None
		self._in1 = None
		return
listRolesRequest.typecode = ZSI.TCcompound.Struct(
	pname=("%s.service.admin"%(nspace),"listRoles"),
	ofwhat=[],
	pyclass=listRolesRequest,
	encoded="%s.service.admin"%(nspace))

class listRolesResponse:
	def __init__(self):
		self._listRolesReturn = None
		return
listRolesResponse.typecode = ZSI.TCcompound.Struct(
	pname=("%s.service.admin"%(nspace),"listRolesResponse"),
	ofwhat=[
		ns1.ArrayOf_soapenc_string_Def(
			pname="listRolesReturn", aname="_listRolesReturn",
			typed=False, encoded=None,
			minOccurs=1, maxOccurs=1, nillable=True)],
	pyclass=listRolesResponse,
	encoded="%s.service.admin"%(nspace))


class listSubGroupsRequest:
	def __init__(self):
		self._in0 = None
		return
listSubGroupsRequest.typecode = ZSI.TCcompound.Struct(
	pname=("%s.service.admin"%(nspace),"listSubGroups"),
	ofwhat=[
		ZSI.TC.String(pname="in0", aname="_in0",
			typed=False, encoded=None,
			minOccurs=1, maxOccurs=1, nillable=True)],
	pyclass=listSubGroupsRequest,
	encoded="%s.service.admin"%(nspace))

class listSubGroupsResponse:
	def __init__(self):
		self._listSubGroupsReturn = None
		return
listSubGroupsResponse.typecode = ZSI.TCcompound.Struct(
	pname=("%s.service.admin"%(nspace),"listSubGroupsResponse"),
	ofwhat=[
		ns1.ArrayOf_soapenc_string_Def(
			pname="listSubGroupsReturn", aname="_listSubGroupsReturn",
			typed=False, encoded=None,
			minOccurs=1, maxOccurs=1, nillable=True)],
	pyclass=listSubGroupsResponse,
	encoded="%s.service.admin"%(nspace))


class SOAPBinding:
	"""Class to use SOAP interface of voms"""
	def __init__(self, **kw):
		transdict = {"cert_file":kw['user_cert'], "key_file":kw['user_key']}
		kw.setdefault("readerclass", None)
		kw.setdefault("writerclass", None)
		admin_url = "https://%s:%d/voms/%s/services/VOMSAdmin" % (
		                                  kw['host'],kw['port'],kw['vo'])
		self.binding = client.Binding(
			url=admin_url, transdict = transdict, **kw)

	# op "list-members"
	def listMembers(self, **kw):
		request = listMembersRequest()
		request._in0 = "%s"%(kw["group"])
		self.binding.Send(None, None, request,
			soapaction="", encodingStyle=encstyle, **kw)
		typecode = ZSI.TCcompound.Struct(
			pname=None, ofwhat=listMembersResponse.typecode.ofwhat,
			pyclass=listMembersResponse.typecode.pyclass)
		if self.binding.Receive(typecode)._listMembersReturn == None:
			return []
		return self.binding.Receive(typecode)._listMembersReturn

	# op "list-sub-groups"
	def listSubGroups(self, **kw):
		request = listSubGroupsRequest()
		request._in0 = "%s"%(kw["group"])
		self.binding.Send(None, None, request,
			soapaction="", encodingStyle=encstyle, **kw)
		typecode = ZSI.TCcompound.Struct(
			pname=None, ofwhat=listSubGroupsResponse.typecode.ofwhat,
			pyclass=listSubGroupsResponse.typecode.pyclass)
		response = self.binding.Receive(typecode)
		if self.binding.Receive(typecode)._listSubGroupsReturn == None:
			return []
		return self.binding.Receive(typecode)._listSubGroupsReturn

	# op "list-roles"
	def listRoles(self, **kw):
		request = listRolesRequest()
		self.binding.Send(None, None, request,
			soapaction="", encodingStyle=encstyle)
		typecode = ZSI.TCcompound.Struct(
			pname=None, ofwhat=listRolesResponse.typecode.ofwhat,
			pyclass=listRolesResponse.typecode.pyclass)
		if self.binding.Receive(typecode)._listRolesReturn == None:
			return []
		return self.binding.Receive(typecode)._listRolesReturn

	# op "list-users-with-role"
	def listUsersWithRole(self, **kw):
		request = listUsersWithRoleRequest()
		request._in0 = "%s"%(kw["group"])
		request._in1 = "%s"%(kw["role"])
		self.binding.Send(None, None, request,
			soapaction="", encodingStyle=encstyle)
		typecode = ZSI.TCcompound.Struct(
			pname=None, ofwhat=listUsersWithRoleResponse.typecode.ofwhat,
			pyclass=listUsersWithRoleResponse.typecode.pyclass)
		response = self.binding.Receive(typecode)
		if self.binding.Receive(typecode)._listUsersWithRoleReturn == None:
			return []
		return self.binding.Receive(typecode)._listUsersWithRoleReturn

			
	def execute(self, cmd, **arg):
		cmds = {"listUsersWithRole":"","listRoles":"",
			"listSubGroups":"","listMembers":""}
		if cmd in cmds:
			try:
				response = self.__class__.__dict__[cmd](self, **arg)
				if isinstance(response, types.ListType):
					return response
			except Exception, e:
				raise e
		else:
			raise Exception("Unkown Command:%s"%(cmd))
