--
-- Copyright (c) 2011  Software Research Associates, Inc.
--
-- Redistribution and use in source and binary forms, with or without
-- modification, are permitted provided that the following conditions
-- are met:
-- 1. Redistributions of source code must retain the above copyright
--    notice, this list of conditions and the following disclaimer.
-- 2. Redistributions in binary form must reproduce the above copyright
--    notice, this list of conditions and the following disclaimer in the
--    documentation and/or other materials provided with the distribution.
-- 3. Neither the name of the project nor the names of its contributors
--    may be used to endorse or promote products derived from this software
--    without specific prior written permission.
-- 
-- THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
-- ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
-- IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
-- ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
-- FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
-- DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
-- OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
-- HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
-- LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
-- OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
-- SUCH DAMAGE.
--

--
-- Gfarm v2 gfm protocol dissector for Wireshark.
-- 
-- How To Use:
--    Edit Wireshark's 'init.lua'. Replace the following line
--
--          disable_lua = true; do return end;
--    with
--
--          -- disable_lua = true; do return end;
--
--    Then, insert the following line at the end of the file:
--   
--          dofile("/somewhere/else/gfarm-gfm.lua")
-- 
--    where "/somewhere/..." is an actual path of this file.
--   
-- Restrictions:
--   1. Packet dissection of some gfm commands have not been implemented yet.
--   2. Fragmentated packet are not dissected correctly.
--

-- NOTE: comment out the following line, if Wireshark >= 2.0 (i.e. Lua >= 5.2)
require("bit")

--
-- Mapping table: command code -> name.
--
local gfm_command_names = {
   [  0] = 'GFM_PROTO_HOST_INFO_GET_ALL', 
   [  1] = 'GFM_PROTO_HOST_INFO_GET_BY_ARCHITECTURE', 
   [  2] = 'GFM_PROTO_HOST_INFO_GET_BY_NAMES', 
   [  3] = 'GFM_PROTO_HOST_INFO_GET_BY_NAMEALIASES', 
   [  4] = 'GFM_PROTO_HOST_INFO_SET', 
   [  5] = 'GFM_PROTO_HOST_INFO_MODIFY', 
   [  6] = 'GFM_PROTO_HOST_INFO_REMOVE', 
   [ 16] = 'GFM_PROTO_USER_INFO_GET_ALL', 
   [ 17] = 'GFM_PROTO_USER_INFO_GET_BY_NAMES', 
   [ 18] = 'GFM_PROTO_USER_INFO_SET', 
   [ 19] = 'GFM_PROTO_USER_INFO_MODIFY', 
   [ 20] = 'GFM_PROTO_USER_INFO_REMOVE', 
   [ 21] = 'GFM_PROTO_USER_INFO_GET_BY_GSI_DN', 
   [ 32] = 'GFM_PROTO_GROUP_INFO_GET_ALL', 
   [ 33] = 'GFM_PROTO_GROUP_INFO_GET_BY_NAMES', 
   [ 34] = 'GFM_PROTO_GROUP_INFO_SET', 
   [ 35] = 'GFM_PROTO_GROUP_INFO_MODIFY', 
   [ 36] = 'GFM_PROTO_GROUP_INFO_REMOVE', 
   [ 37] = 'GFM_PROTO_GROUP_INFO_ADD_USERS', 
   [ 38] = 'GFM_PROTO_GROUP_INFO_REMOVE_USERS', 
   [ 39] = 'GFM_PROTO_GROUP_NAMES_GET_BY_USERS', 
   [ 48] = 'GFM_PROTO_QUOTA_USER_GET', 
   [ 49] = 'GFM_PROTO_QUOTA_USER_SET', 
   [ 50] = 'GFM_PROTO_QUOTA_GROUP_GET', 
   [ 51] = 'GFM_PROTO_QUOTA_GROUP_SET', 
   [ 52] = 'GFM_PROTO_QUOTA_CHECK', 
   [ 54] = 'GFM_PROTO_DIRSET_INFO_SET',
   [ 55] = 'GFM_PROTO_DIRSET_INFO_REMOVE',
   [ 56] = 'GFM_PROTO_DIRSET_INFO_LIST',
   [ 57] = 'GFM_PROTO_QUOTA_DIRSET_GET',
   [ 58] = 'GFM_PROTO_QUOTA_DIRSET_SET',
   [ 59] = 'GFM_PROTO_QUOTA_DIR_GET',
   [ 60] = 'GFM_PROTO_QUOTA_DIR_SET',
   [ 61] = 'GFM_PROTO_DIRSET_DIR_LIST',
   [ 64] = 'GFM_PROTO_COMPOUND_BEGIN', 
   [ 65] = 'GFM_PROTO_COMPOUND_END', 
   [ 66] = 'GFM_PROTO_COMPOUND_ON_ERROR', 
   [ 67] = 'GFM_PROTO_PUT_FD', 
   [ 68] = 'GFM_PROTO_GET_FD', 
   [ 69] = 'GFM_PROTO_SAVE_FD', 
   [ 70] = 'GFM_PROTO_RESTORE_FD', 
   [ 71] = 'GFM_PROTO_BEQUEATH_FD', 
   [ 72] = 'GFM_PROTO_INHERIT_FD', 
   [ 80] = 'GFM_PROTO_OPEN_ROOT', 
   [ 81] = 'GFM_PROTO_OPEN_PARENT', 
   [ 82] = 'GFM_PROTO_OPEN', 
   [ 83] = 'GFM_PROTO_CREATE', 
   [ 84] = 'GFM_PROTO_CLOSE', 
   [ 85] = 'GFM_PROTO_VERIFY_TYPE', 
   [ 86] = 'GFM_PROTO_VERIFY_TYPE_NOT', 
   [ 87] = 'GFM_PROTO_REVOKE_GFSD_ACCESS', 
   [ 96] = 'GFM_PROTO_FSTAT', 
   [ 97] = 'GFM_PROTO_FUTIMES', 
   [ 98] = 'GFM_PROTO_FCHMOD', 
   [ 99] = 'GFM_PROTO_FCHOWN', 
   [100] = 'GFM_PROTO_CKSUM_GET', 
   [101] = 'GFM_PROTO_CKSUM_SET', 
   [102] = 'GFM_PROTO_SCHEDULE_FILE', 
   [103] = 'GFM_PROTO_SCHEDULE_FILE_WITH_PROGRAM', 
   [104] = 'GFM_PROTO_FGETATTRPLUS', 
   [112] = 'GFM_PROTO_REMOVE', 
   [113] = 'GFM_PROTO_RENAME', 
   [114] = 'GFM_PROTO_FLINK', 
   [115] = 'GFM_PROTO_MKDIR', 
   [116] = 'GFM_PROTO_SYMLINK', 
   [117] = 'GFM_PROTO_READLINK', 
   [118] = 'GFM_PROTO_GETDIRPATH', 
   [119] = 'GFM_PROTO_GETDIRENTS', 
   [120] = 'GFM_PROTO_SEEK', 
   [121] = 'GFM_PROTO_GETDIRENTSPLUS', 
   [122] = 'GFM_PROTO_GETDIRENTSPLUSXATTR', 
   [128] = 'GFM_PROTO_REOPEN', 
   [129] = 'GFM_PROTO_CLOSE_READ', 
   [130] = 'GFM_PROTO_CLOSE_WRITE', 
   [131] = 'GFM_PROTO_LOCK', 
   [132] = 'GFM_PROTO_TRYLOCK', 
   [133] = 'GFM_PROTO_UNLOCK', 
   [134] = 'GFM_PROTO_LOCK_INFO', 
   [135] = 'GFM_PROTO_SWITCH_BACK_CHANNEL', 
   [136] = 'GFM_PROTO_SWITCH_ASYNC_BACK_CHANNEL', 
   [137] = 'GFM_PROTO_CLOSE_WRITE_V2_4', 
   [138] = 'GFM_PROTO_GENERATION_UPDATED', 
   [139] = 'GFM_PROTO_FHCLOSE_READ', 
   [140] = 'GFM_PROTO_FHCLOSE_WRITE', 
   [141] = 'GFM_PROTO_GENERATION_UPDATED_BY_COOKIE', 
   [144] = 'GFM_PROTO_GLOB', 
   [145] = 'GFM_PROTO_SCHEDULE', 
   [146] = 'GFM_PROTO_PIO_OPEN', 
   [147] = 'GFM_PROTO_PIO_SET_PATHS', 
   [148] = 'GFM_PROTO_PIO_CLOSE', 
   [149] = 'GFM_PROTO_PIO_VISIT', 
   [176] = 'GFM_PROTO_HOSTNAME_SET', 
   [177] = 'GFM_PROTO_SCHEDULE_HOST_DOMAIN', 
   [178] = 'GFM_PROTO_STATFS', 
   [192] = 'GFM_PROTO_REPLICA_LIST_BY_NAME', 
   [193] = 'GFM_PROTO_REPLICA_LIST_BY_HOST', 
   [194] = 'GFM_PROTO_REPLICA_REMOVE_BY_HOST', 
   [195] = 'GFM_PROTO_REPLICA_REMOVE_BY_FILE', 
   [196] = 'GFM_PROTO_REPLICA_INFO_GET', 
   [197] = 'GFM_PROTO_REPLICATE_FILE_FROM_TO', 
   [198] = 'GFM_PROTO_REPLICATE_FILE_TO', 
   [208] = 'GFM_PROTO_REPLICA_ADDING', 
   [209] = 'GFM_PROTO_REPLICA_ADDED', 
   [210] = 'GFM_PROTO_REPLICA_LOST', 
   [211] = 'GFM_PROTO_REPLICA_ADD', 
   [212] = 'GFM_PROTO_REPLICA_ADDED2', 
   [213] = 'GFM_PROTO_REPLICATION_RESULT', 
   [224] = 'GFM_PROTO_PROCESS_ALLOC', 
   [225] = 'GFM_PROTO_PROCESS_ALLOC_CHILD', 
   [226] = 'GFM_PROTO_PROCESS_FREE', 
   [227] = 'GFM_PROTO_PROCESS_SET', 
   [240] = 'GFJ_PROTO_LOCK_REGISTER', 
   [241] = 'GFJ_PROTO_UNLOCK_REGISTER', 
   [242] = 'GFJ_PROTO_REGISTER', 
   [243] = 'GFJ_PROTO_UNREGISTER', 
   [244] = 'GFJ_PROTO_REGISTER_NODE', 
   [245] = 'GFJ_PROTO_LIST', 
   [246] = 'GFJ_PROTO_INFO', 
   [247] = 'GFJ_PROTO_HOSTINFO', 
   [256] = 'GFM_PROTO_XATTR_SET', 
   [257] = 'GFM_PROTO_XMLATTR_SET', 
   [258] = 'GFM_PROTO_XATTR_GET', 
   [259] = 'GFM_PROTO_XMLATTR_GET', 
   [260] = 'GFM_PROTO_XATTR_REMOVE', 
   [261] = 'GFM_PROTO_XMLATTR_REMOVE', 
   [262] = 'GFM_PROTO_XATTR_LIST', 
   [263] = 'GFM_PROTO_XMLATTR_LIST', 
   [264] = 'GFM_PROTO_XMLATTR_FIND', 
   [272] = 'GFM_PROTO_SWITCH_GFMD_CHANNEL', 
   [273] = 'GFM_PROTO_JOURNAL_READY_TO_RECV', 
   [274] = 'GFM_PROTO_JOURNAL_SEND', 
   [288] = 'GFM_PROTO_METADB_SERVER_GET', 
   [289] = 'GFM_PROTO_METADB_SERVER_GET_ALL', 
   [290] = 'GFM_PROTO_METADB_SERVER_SET', 
   [291] = 'GFM_PROTO_METADB_SERVER_MODIFY', 
   [292] = 'GFM_PROTO_METADB_SERVER_REMOVE',
}

--
-- Mapping table: error code -> name.
--
local error_names = {
   [  0] = 'GFARM_ERR_NO_ERROR', 
   [  1] = 'GFARM_ERR_OPERATION_NOT_PERMITTED', 
   [  2] = 'GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY', 
   [  3] = 'GFARM_ERR_NO_SUCH_PROCESS', 
   [  4] = 'GFARM_ERR_INTERRUPTED_SYSTEM_CALL', 
   [  5] = 'GFARM_ERR_INPUT_OUTPUT', 
   [  6] = 'GFARM_ERR_DEVICE_NOT_CONFIGURED', 
   [  7] = 'GFARM_ERR_ARGUMENT_LIST_TOO_LONG', 
   [  8] = 'GFARM_ERR_EXEC_FORMAT', 
   [  9] = 'GFARM_ERR_BAD_FILE_DESCRIPTOR', 
   [ 10] = 'GFARM_ERR_NO_CHILD_PROCESS', 
   [ 11] = 'GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE', 
   [ 12] = 'GFARM_ERR_NO_MEMORY', 
   [ 13] = 'GFARM_ERR_PERMISSION_DENIED', 
   [ 14] = 'GFARM_ERR_BAD_ADDRESS', 
   [ 15] = 'GFARM_ERR_BLOCK_DEVICE_REQUIRED', 
   [ 16] = 'GFARM_ERR_DEVICE_BUSY', 
   [ 17] = 'GFARM_ERR_ALREADY_EXISTS', 
   [ 18] = 'GFARM_ERR_CROSS_DEVICE_LINK', 
   [ 19] = 'GFARM_ERR_OPERATION_NOT_SUPPORTED_BY_DEVICE', 
   [ 20] = 'GFARM_ERR_NOT_A_DIRECTORY', 
   [ 21] = 'GFARM_ERR_IS_A_DIRECTORY', 
   [ 22] = 'GFARM_ERR_INVALID_ARGUMENT', 
   [ 23] = 'GFARM_ERR_TOO_MANY_OPEN_FILES_IN_SYSTEM', 
   [ 24] = 'GFARM_ERR_TOO_MANY_OPEN_FILES', 
   [ 25] = 'GFARM_ERR_INAPPROPRIATE_IOCTL_FOR_DEVICE', 
   [ 26] = 'GFARM_ERR_TEXT_FILE_BUSY', 
   [ 27] = 'GFARM_ERR_FILE_TOO_LARGE', 
   [ 28] = 'GFARM_ERR_NO_SPACE', 
   [ 29] = 'GFARM_ERR_ILLEGAL_SEEK', 
   [ 30] = 'GFARM_ERR_READ_ONLY_FILE_SYSTEM', 
   [ 31] = 'GFARM_ERR_TOO_MANY_LINKS', 
   [ 32] = 'GFARM_ERR_BROKEN_PIPE', 
   [ 33] = 'GFARM_ERR_NUMERICAL_ARGUMENT_OUT_OF_DOMAIN', 
   [ 34] = 'GFARM_ERR_RESULT_OUT_OF_RANGE', 
   [ 35] = 'GFARM_ERR_ILLEGAL_BYTE_SEQUENCE', 
   [ 36] = 'GFARM_ERR_RESOURCE_DEADLOCK_AVOIDED', 
   [ 37] = 'GFARM_ERR_FILE_NAME_TOO_LONG', 
   [ 38] = 'GFARM_ERR_DIRECTORY_NOT_EMPTY', 
   [ 39] = 'GFARM_ERR_NO_LOCKS_AVAILABLE', 
   [ 40] = 'GFARM_ERR_FUNCTION_NOT_IMPLEMENTED', 
   [ 41] = 'GFARM_ERR_OPERATION_NOW_IN_PROGRESS', 
   [ 42] = 'GFARM_ERR_OPERATION_ALREADY_IN_PROGRESS', 
   [ 43] = 'GFARM_ERR_SOCKET_OPERATION_ON_NON_SOCKET', 
   [ 44] = 'GFARM_ERR_DESTINATION_ADDRESS_REQUIRED', 
   [ 45] = 'GFARM_ERR_MESSAGE_TOO_LONG', 
   [ 46] = 'GFARM_ERR_PROTOCOL_WRONG_TYPE_FOR_SOCKET', 
   [ 47] = 'GFARM_ERR_PROTOCOL_NOT_AVAILABLE', 
   [ 48] = 'GFARM_ERR_PROTOCOL_NOT_SUPPORTED', 
   [ 49] = 'GFARM_ERR_OPERATION_NOT_SUPPORTED', 
   [ 50] = 'GFARM_ERR_ADDRESS_FAMILY_NOT_SUPPORTED_BY_PROTOCOL_FAMILY', 
   [ 51] = 'GFARM_ERR_ADDRESS_ALREADY_IN_USE', 
   [ 52] = 'GFARM_ERR_CANNOT_ASSIGN_REQUESTED_ADDRESS', 
   [ 53] = 'GFARM_ERR_NETWORK_IS_DOWN', 
   [ 54] = 'GFARM_ERR_NETWORK_IS_UNREACHABLE', 
   [ 55] = 'GFARM_ERR_CONNECTION_ABORTED', 
   [ 56] = 'GFARM_ERR_CONNECTION_RESET_BY_PEER', 
   [ 57] = 'GFARM_ERR_NO_BUFFER_SPACE_AVAILABLE', 
   [ 58] = 'GFARM_ERR_SOCKET_IS_ALREADY_CONNECTED', 
   [ 59] = 'GFARM_ERR_SOCKET_IS_NOT_CONNECTED', 
   [ 60] = 'GFARM_ERR_OPERATION_TIMED_OUT', 
   [ 61] = 'GFARM_ERR_CONNECTION_REFUSED', 
   [ 62] = 'GFARM_ERR_NO_ROUTE_TO_HOST', 
   [ 63] = 'GFARM_ERR_TOO_MANY_LEVELS_OF_SYMBOLIC_LINK', 
   [ 64] = 'GFARM_ERR_DISK_QUOTA_EXCEEDED', 
   [ 65] = 'GFARM_ERR_STALE_FILE_HANDLE', 
   [ 66] = 'GFARM_ERR_IDENTIFIER_REMOVED', 
   [ 67] = 'GFARM_ERR_NO_MESSAGE_OF_DESIRED_TYPE', 
   [ 68] = 'GFARM_ERR_VALUE_TOO_LARGE_TO_BE_STORED_IN_DATA_TYPE', 
   [ 69] = 'GFARM_ERR_AUTHENTICATION', 
   [ 70] = 'GFARM_ERR_EXPIRED', 
   [ 71] = 'GFARM_ERR_PROTOCOL', 
   [ 72] = 'GFARM_ERR_UNKNOWN_HOST', 
   [ 73] = 'GFARM_ERR_CANNOT_RESOLVE_AN_IP_ADDRESS_INTO_A_HOSTNAME', 
   [ 74] = 'GFARM_ERR_NO_SUCH_OBJECT', 
   [ 75] = 'GFARM_ERR_CANT_OPEN', 
   [ 76] = 'GFARM_ERR_UNEXPECTED_EOF', 
   [ 77] = 'GFARM_ERR_GFARM_URL_PREFIX_IS_MISSING', 
   [ 78] = 'GFARM_ERR_TOO_MANY_JOBS', 
   [ 79] = 'GFARM_ERR_FILE_MIGRATED', 
   [ 80] = 'GFARM_ERR_NOT_A_SYMBOLIC_LINK', 
   [ 81] = 'GFARM_ERR_IS_A_SYMBOLIC_LINK', 
   [ 82] = 'GFARM_ERR_UNKNOWN', 
   [ 83] = 'GFARM_ERR_INVALID_FILE_REPLICA', 
   [ 84] = 'GFARM_ERR_NO_SUCH_USER', 
   [ 85] = 'GFARM_ERR_CANNOT_REMOVE_LAST_REPLICA', 
   [ 86] = 'GFARM_ERR_NO_SUCH_GROUP', 
   [ 87] = 'GFARM_ERR_GFARM_URL_USER_IS_MISSING', 
   [ 88] = 'GFARM_ERR_GFARM_URL_HOST_IS_MISSING', 
   [ 89] = 'GFARM_ERR_GFARM_URL_PORT_IS_MISSING', 
   [ 90] = 'GFARM_ERR_GFARM_URL_PORT_IS_INVALID', 
   [ 91] = 'GFARM_ERR_FILE_BUSY', 
   [ 92] = 'GFARM_ERR_NOT_A_REGULAR_FILE', 
   [ 93] = 'GFARM_ERR_IS_A_REGULAR_FILE', 
   [ 94] = 'GFARM_ERR_PATH_IS_ROOT', 
   [ 95] = 'GFARM_ERR_INTERNAL_ERROR', 
   [ 96] = 'GFARM_ERR_DB_ACCESS_SHOULD_BE_RETRIED', 
   [ 97] = 'GFARM_ERR_TOO_MANY_HOSTS', 
   [ 98] = 'GFARM_ERR_GFMD_FAILED_OVER', 
   [ 99] = 'GFARM_ERR_BAD_INODE_NUMBER', 
   [100] = 'GFARM_ERR_BAD_COOKIE', 
   [101] = 'GFARM_ERR_INSUFFICIENT_NUMBER_OF_FILE_REPLICAS',
   [102] = 'GFARM_ERR_CHECKSUM_MISMATCH',
   [103] = 'GFARM_ERR_CONFLICT_DETECTED',
   [104] = 'GFARM_ERR_INVALID_CREDENTIAL',
   [105] = 'GFARM_ERR_NO_FILESYSTEM_NODE',
   [106] = 'GFARM_ERR_DIRECTORY_QUOTA_EXISTS',
}

--
-- Mapping table: whence value -> symbol.
--
local whence_names = {
   [0] = SEEK_SET,
   [1] = SEEK_CUR,
   [2] = SEEK_END,
}

--
-- Proto.
--
local gfm_port = 601
local gfm_proto = Proto("gfarm-gfm", "gfarm gfm protocol")

--
-- Protofields.
--
local uint32_field = ProtoField.string("gfm.parameter.uint32",
                                       "uint32 parameter",
                                       base.DEC)
local uint64_field = ProtoField.string("gfm.parameter.uint32",
                                       "uint32 parameter",
                                       base.DEC)
local string_field = ProtoField.string("gfm.parameter.string",
                                       "string parameter")
local bytearray_field = ProtoField.bytes("gfm.parameter.bytes",
                                         "bytestream parameter")

gfm_proto.fields = {
   uint32_field,
   uint64_field,
   string_field,
   bytearray_field,
}

--
-- Parse one XDR field.
--
function parse_xdr(tvb, item, xdr_type, offset, text, map)
   if offset < 0 then
      return offset
   end

   if xdr_type == "i" then
      if offset + 4 > tvb():len() then
         return -1, 0
      end
      local value = tvb(offset, 4):uint()
      local subitem = item:add(uint32_field, tvb(offset, 4))
      if type(map) == "function" then
	 subitem:set_text(string.format("%-20s: %s", text, map(value)))
      elseif type(map) == "table" then
         local mapvalue = map[value]
         if mapvalue == nil then
            mapvalue = "unknown"
         end
         subitem:set_text(string.format("%-20s: %s (0x%08x)",
                                        text, mapvalue, value))
      elseif map == base.OCT then
         subitem:set_text(string.format("%-20s: %012o", text, value))
      elseif map == base.DEC then
         subitem:set_text(string.format("%-20s: %d", text, value))
      elseif map == base.HEX then
         subitem:set_text(string.format("%-20s: %08x", text, value))
      else
         subitem:set_text(string.format("%-20s: %u", text, value))
      end
      return offset + 4, value

   elseif xdr_type == "l" then
      if offset + 8 > tvb():len() then
         return -1, 0, 0
      end
      local value1 = tvb(offset, 4):uint()
      local value2 = tvb(offset + 4, 4):uint()
      local subitem = item:add(uint64_field, tvb(offset, 8))
      if type(map) == "function" then
	 subitem:set_text(string.format("%-20s: %s", text,
					map(value1, value2)))
      elseif map == base.HEX or value1 ~= 0 then
         subitem:set_text(string.format("%-20s: 0x%08x%08x",
                                        text, value1, value2))
      elseif map == base.OCT then
         subitem:set_text(string.format("%-20s: %012o", text, value2))
      else
         subitem:set_text(string.format("%-20s: %u", text, value2))
      end
      return offset + 8, value1, value2

   elseif xdr_type == "s" then
      if offset + 4 > tvb():len() then
         return -1, 0
      end
      local len = tvb(offset, 4):uint()
      if offset + 4 + len > tvb():len() then
         return -1
      end      
      local value = tvb(offset + 4, len):string()
      local subitem = item:add(string_field, tvb(offset, 4 + len))
      subitem:set_text(string.format("%-20s: \"%s\"", text, value))
      return offset + 4 + len, len

   else
      if offset + 4 > tvb():len() then
         return -1, 0
      end
      local len = tvb(offset, 4):uint()
      if offset + 4 + len > tvb():len() then
         return -1
      end      
      local subitem = item:add(bytearray_field, tvb(offset, 4 + len))
      subitem:set_text(string.format("%-20s: byte array [%s byte(s)]",
                                     text, len))
      return offset + 4 + len, len
   end
end

--
-- Format GFM_PROTO_CKSUM_GET flags.
--
function format_gfm_cksum_get_flags(value)
   local flags = {}
   if bit.band(value, 0x01) ~= 0 then table.insert(flags, "MAYBE_EXPIRED") end
   if bit.band(value, 0x02) ~= 0 then table.insert(flags, "EXPIRED")       end
   return table.concat(flags)
end

--
-- Format GFM_PROTO_CKSUM_SET flags.
--
function format_gfm_cksum_set_flags(value)
   local flags = {}
   if bit.band(value, 0x01) ~= 0 then table.insert(flags, "MODIFIED") end
   return table.concat(flags)
end

--
-- Format GFM_PROTO_SCHEDULE_FILE flags.
--
function format_gfm_sched_flags(value)
   local flags = {}
   if bit.band(value, 0x01) ~= 0 then table.insert(flags, "HOST_AVAIL")    end
   if bit.band(value, 0x02) ~= 0 then table.insert(flags, "LOADAVG_AVAIL") end
   if bit.band(value, 0x04) ~= 0 then table.insert(flags, "RTT_AVAIL")     end
   return table.concat(flags)
end

--
-- Format GFM_PROTO_CLOSE_WRITE_V2_4 flags.
--
function format_gfm_write_v2_4_flags(value)
   local flags = {}
   if bit.band(value, 0x01) ~= 0 then
      table.insert(flags, "GENERATION_UPDATE_NEEDED")
   end
   return table.concat(flags)
end

--
-- Format GFM_PROTO_METADB_SERVER_GET: Persistent Flags.
--
function format_gfm_metadb_server_persistent_flags(value)
   local flags = {}
   if bit.band(value, 0x01) ~= 0 then
      table.insert(flags, "IS_MASTER_CANDIDATE")
   end
   if bit.band(value, 0x02) ~= 0 then
      table.insert(flags, "IS_DEFAULT_MASTER")
   end
   return table.concat(flags)
end

--
-- Format GFM_PROTO_METADB_SERVER_GET: Volatile Flags.
--
function format_gfm_metadb_server_volatile_flags(value)
   local flags = {}
   if bit.band(value, 0x01) ~= 0 then table.insert(flags, "IS_SELF")    end
   if bit.band(value, 0x02) ~= 0 then table.insert(flags, "IS_MASTER")  end
   if bit.band(value, 0x04) ~= 0 then table.insert(flags, "IS_SYNCREP") end
   if bit.band(value, 0x08) ~= 0 then table.insert(flags, "IS_ACTIVE")  end

   local seqnum = bit.band(value,  0x0070)
   if seqnum == 0 then
      table.insert(flags, "SEQNUM_IS_UNKNOWN")
   elseif seqnum == 1 then
      table.insert(flags, "SEQNUM_IS_OK")
   elseif seqnum == 1 then
      table.insert(flags, "SEQNUM_IS_OUT_OF_SYNC")
   else
      table.insert(flags, "SEQNUM_IS_ERROR")
   end
   return table.concat(flags)
end

--
-- Format datetime.
--
function format_datetime(value1, value2)
   if value1 ~= 0 then
      return string.format("%-20s: 0x%08x%08x", value1, value2)
   else
      return os.date("%Y-%m-%d %H:%M:%S", value2)
   end
end

--
-- Parse a response which return an error code only.
--
function parse_response_with_error_code_only(tvb, pinfo, item, offset)
   return parse_xdr(tvb, item, "i", offset, "error_code", error_names)
end

--
-- Parse GFM_PROTO_HOST_INFO_GET_ALL.
--
function parse_gfm_host_info_get_all_request(tvb, pinfo, item, offset)
   -- IN (none)
   return offset + 4
end

function parse_gfm_host_info_get_all_response(tvb, pinfo, item, offset)
   -- OUT error_code
   --     (upon success) i:n_hosts
   --                    s[n_hosts]:hostname, i[n_hosts]:n_host_aliases,
   --                    s[n_hosts][n_host_aliases]:host_aliases
   --                    s[n_hosts]:architecture, i[n_hosts]:ncpu,
   --                    i[n_hosts]:port, i[n_hosts]:flags
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      local n_hosts
      offset, n_hosts = parse_xdr(tvb, item, "i", offset, "n_hosts")
      for i = 1, n_hosts do
         offset = parse_xdr(tvb, item, "s", offset, "hostname")
         offset = parse_xdr(tvb, item, "s", offset, "architecture")
         offset = parse_xdr(tvb, item, "i", offset, "ncpu")
         offset = parse_xdr(tvb, item, "i", offset, "port")
         offset = parse_xdr(tvb, item, "i", offset, "flags")
      end
   end
   return offset
end

--
-- Parse GFM_PROTO_HOST_INFO_GET_BY_ARCHITECTURE.
--
function parse_gfm_host_info_get_by_architecture_request(tvb, pinfo, item, offset)
   -- IN s:architecture
   return nil
   -- offset = offset + 4
   -- offset = parse_xdr(tvb, item, "s", offset, "architecture")
   -- return offset
end

function parse_gfm_host_info_get_by_architecture_response(tvb, pinfo, item, offset)
   return nil
   -- return parse_gfm_host_info_get_all_response(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_HOST_INFO_GET_BY_NAMES.
--
function parse_gfm_host_info_get_by_names_request(tvb, pinfo, item, offset)
   -- IN i: n_hostnames, s[n_hostnames]:hostnames
   offset = offset + 4
   local n_hostnames
   offset, n_hostnames = parse_xdr(tvb, item, "i", offset, "n_hostnames")
   for i = 1, n_hostnames do
      offset = parse_xdr(tvb, item, "s", offset, "hostname")
   end
   return offset
end

function parse_gfm_host_info_get_by_names_response(tvb, pinfo, item, offset)
   return parse_gfm_host_info_get_all_response(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_HOST_INFO_GET_BY_NAMEALIASES.
--
function parse_gfm_host_info_get_by_namealiases_request(tvb, pinfo, item, offset)
   -- IN i: n_hostnames, s[n_hostnames]:hostnames
   return parse_gfm_host_info_get_by_names_request(tvb, pinfo, item, offset)
end

function parse_gfm_host_info_get_by_namealiases_response(tvb, pinfo, item, offset)
   return parse_gfm_host_info_get_all_response(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_HOST_INFO_SET.
--
function parse_gfm_host_info_set_request(tvb, pinfo, item, offset)
   -- IN s:hostname,
   --    s:hostname, s:architecture
   --    i:ncpu, i:port, i:flags
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "hostname")
   offset = parse_xdr(tvb, item, "s", offset, "architecture")
   offset = parse_xdr(tvb, item, "i", offset, "ncpu")
   offset = parse_xdr(tvb, item, "i", offset, "port")
   offset = parse_xdr(tvb, item, "i", offset, "flags")
   return offset
end

function parse_gfm_host_info_set_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_HOST_INFO_MODIFY.
--
function parse_gfm_host_info_modify_request(tvb, pinfo, item, offset)
   -- IN s:hostname,
   --    i:n_host_aliases, s[n_host_aliases]:host_aliases,
   --    s:architecture, i:ncpu, i:port, i:flags
   return parse_gfm_host_info_set_request(tvb, pinfo, item, offset)
end

function parse_gfm_host_info_modify_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_HOST_INFO_REMOVE.
--
function parse_gfm_host_info_remove_request(tvb, pinfo, item, offset)
   -- IN s:hostname
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "hostname")
   return offset
end

function parse_gfm_host_info_remove_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_USER_INFO_GET_ALL.
--
function parse_gfm_user_info_get_all_request(tvb, pinfo, item, offset)
   -- IN (none)
   return offset + 4
end

function parse_gfm_user_info_get_all_response(tvb, pinfo, item, offset)
   -- OUT error_code
   --     (upon success) i:n_users
   --                    s[n_users]:username, s[n_users]:real_name
   --                    s[n_users]:gfarm_homedir, s[n_users]:gsi_dn
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      local n_users
      offset, n_users = parse_xdr(tvb, item, "i", offset, "n_users")
      for i = 1, n_users do
         offset = parse_xdr(tvb, item, "s", offset, "username")
         offset = parse_xdr(tvb, item, "s", offset, "real_name")
         offset = parse_xdr(tvb, item, "s", offset, "gfarm_homedir")
         offset = parse_xdr(tvb, item, "s", offset, "gsi_dn")
      end
   end
   return offset
end

--
-- Parse GFM_PROTO_USER_INFO_GET_BY_NAMES.
--
function parse_gfm_user_info_get_by_names_request(tvb, pinfo, item, offset)
   -- IN i:n_usernames, s[n_usernames]:usernames
   offset = offset + 4
   local n_usernames
   offset, n_usernames = parse_xdr(tvb, item, "i", offset, "n_usernames")

   for i = 1, n_usernames do
      offset = parse_xdr(tvb, item, "s", offset, "username")
   end
   return offset
end

function parse_gfm_user_info_get_by_names_response(tvb, pinfo, item, offset)
   return parse_gfm_user_info_get_all_response(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_USER_INFO_SET.
--
function parse_gfm_user_info_set_request(tvb, pinfo, item, offset)
   -- IN s:username,
   --    s:real_name, s:gfarm_homedir, s:gsi_dn
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "username")
   offset = parse_xdr(tvb, item, "s", offset, "gfarm_homedir")
   offset = parse_xdr(tvb, item, "s", offset, "gsi_dn")
   return offset
end

function parse_gfm_user_info_set_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_USER_INFO_MODIFY.
--
function parse_gfm_user_info_modify_request(tvb, pinfo, item, offset)
   -- IN s:username,
   --    s:real_name, s:gfarm_homedir, s:gsi_dn
   return parse_gfm_user_info_set_request(tvb, pinfo, item, offset)
end

function parse_gfm_user_info_modify_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_USER_INFO_REMOVE.
--
function parse_gfm_user_info_remove_request(tvb, pinfo, item, offset)
   -- IN s:username
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "username")
   return offset
end

function parse_gfm_user_info_remove_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_USER_INFO_GET_BY_GSI_DN.
--
function parse_gfm_user_info_get_by_gsi_dn_request(tvb, pinfo, item, offset)
   -- IN s:dn (undocumented)
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "dn")
   return offset + 4
end

function parse_gfm_user_info_get_by_gsi_dn_response(tvb, pinfo, item, offset)
   -- OUT error_code
   --     (upon success) s:username, s:real_name
   --                    s:gfarm_homedir, s:gsi_dn
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      offset = parse_xdr(tvb, item, "s", offset, "username")
      offset = parse_xdr(tvb, item, "s", offset, "real_name")
      offset = parse_xdr(tvb, item, "s", offset, "gfarm_homedir")
      offset = parse_xdr(tvb, item, "s", offset, "gsi_dn")
   end
   return offset
end

--
-- Parse GFM_PROTO_GROUP_INFO_GET_ALL.
--
function parse_gfm_group_info_get_all_request(tvb, pinfo, item, offset)
   -- IN (none)
   return offset + 4
end

function parse_gfm_group_info_get_all_response(tvb, pinfo, item, offset)
   -- OUT error_code
   --     (upon success) i:n_groups, s[n_groups]:groupname,
   --                    i[n_groups]:n_users, s[n_groups][n_users]:users
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      local n_groups
      offset, n_groups = parse_xdr(tvb, item, "i", offset, "n_groups")
      for i = 1, n_groups do
         offset = parse_xdr(tvb, item, "s", offset, "groupname")
         local n_users
         offset, n_users = parse_xdr(tvb, item, "i", offset, "n_users")
         for j = 1, n_users do
            offset = parse_xdr(tvb, item, "s", offset, "user")
         end
      end
   end
   return offset
end

--
-- Parse GFM_PROTO_GROUP_INFO_GET_BY_NAMES.
--
function parse_gfm_group_info_get_by_names_request(tvb, pinfo, item, offset)
   -- IN i:n_groupnames, s[n_groupnames]:groupnames
   offset = offset + 4
   local n_groupnames
   offset, n_groupnames = parse_xdr(tvb, item, "i", offset, "n_groupnames")
   for i = 1, n_groupnames do
      offset = parse_xdr(tvb, item, "s", offset, "groupname")
   end
   return offset
end

function parse_gfm_group_info_get_by_names_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_GROUP_INFO_SET.
--
function parse_gfm_group_info_set_request(tvb, pinfo, item, offset)
   -- IN s:groupname,
   --    i:n_users, s[n_users]:users
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "groupname")
   local n_users
   offset, n_users = parse_xdr(tvb, item, "i", offset, "n_users")
   for i = 1, n_users do
      offset = parse_xdr(tvb, item, "s", offset, "user")
   end
   return offset
end

function parse_gfm_group_info_set_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_GROUP_INFO_MODIFY.
--
function parse_gfm_group_info_modify_request(tvb, pinfo, item, offset)
   -- IN s:groupname,
   --    i:n_users, s[n_users]:users
   return parse_gfm_group_info_set_request(tvb, pinfo, item, offset)
end

function parse_gfm_group_info_modify_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_GROUP_INFO_REMOVE.
--
function parse_gfm_group_info_remove_request(tvb, pinfo, item, offset)
   -- IN s:groupname
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "groupname")
   return offset
end

function parse_gfm_group_info_remove_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_GROUP_INFO_ADD_USERS.
--
function parse_gfm_group_info_add_users_request(tvb, pinfo, item, offset)
   -- IN s:groupname,
   --    i:n_users, s[n_users]:users
   return parse_gfm_group_info_set_request(tvb, pinfo, item, offset)
end

function parse_gfm_group_info_add_users_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_GROUP_INFO_REMOVE_USERS.
--
function parse_gfm_group_info_remove_users_request(tvb, pinfo, item, offset)
   -- IN s:groupname,
   --    i:n_users, s[n_users]:users
   return parse_gfm_group_info_set_request(tvb, pinfo, item, offset)
end

function parse_gfm_group_info_remove_users_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_GROUP_NAMES_GET_BY_USERS.
--
function parse_gfm_group_names_get_by_users_request(tvb, pinfo, item, offset)
   -- IN i:n_usernames, s[n_usernames]:usernames
   return parse_gfm_user_info_get_by_names_request(tvb, pinfo, item, offset)
end

function parse_gfm_group_names_get_by_users_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_QUOTA_USER_GET.
--
function parse_gfm_quota_user_get_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_quota_user_get_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_QUOTA_USER_SET.
--
function parse_gfm_quota_user_set_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_quota_user_set_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_QUOTA_GROUP_GET.
--
function parse_gfm_quota_group_get_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_quota_group_get_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_QUOTA_GROUP_SET.
--
function parse_gfm_quota_group_set_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_quota_group_set_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_QUOTA_CHECK.
--
function parse_gfm_quota_check_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_quota_check_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_DIRSET_INFO_SET.
--
function parse_gfm_dirset_info_set_request(tvb, pinfo, item, offset)
   -- IN s:username, s:dirsetname
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "username")
   offset = parse_xdr(tvb, item, "s", offset, "dirsetname")
   return offset
end

function parse_gfm_dirset_info_set_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_DIRSET_INFO_REMOVE.
--
function parse_gfm_dirset_info_remove_request(tvb, pinfo, item, offset)
   -- IN s:username, s:dirsetname
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "username")
   offset = parse_xdr(tvb, item, "s", offset, "dirsetname")
   return offset
end

function parse_gfm_dirset_info_remove_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_DIRSET_INFO_LIST.
--
function parse_gfm_dirset_info_list_request(tvb, pinfo, item, offset)
   -- IN s:username
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "username")
   return offset
end

function parse_gfm_dirset_info_list_response(tvb, pinfo, item, offset)
   -- OUT i:error_code
   --     (upon success) i:n_dirsets,
   --                    s[n_dirset]:username, s[n_dirset]:dirsetname
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      local n_dirsets
      offset, n_dirsets = parse_xdr(tvb, item, "i", offset, "n_dirsets")
      for i = 1, n_dirsets do
         offset = parse_xdr(tvb, item, "s", offset, "username")
         offset = parse_xdr(tvb, item, "s", offset, "dirsetname")
      end
   end
   return offset
end

--
-- Parse GFM_PROTO_QUOTA_DIRSET_GET.
--
function parse_gfm_quota_dirset_get_request(tvb, pinfo, item, offset)
   -- IN s:username, s:dirsetname
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "username")
   offset = parse_xdr(tvb, item, "s", offset, "dirsetname")
   return offset
end

function parse_gfm_quota_dirset_get_response(tvb, pinfo, item, offset)
   -- OUT i:error_code
   --     (upon success)
   --			l:flags
   --			l:grace_period, l:space, l:space_grace,
   --			l:space_soft, l:space_hard, l:num, l:num_grace,
   --			l:num_soft, l:num_hard, l:phy_space,
   --			l:phy_space_grace, l:phy_space_soft,
   --			l:phy_space_hard, l:phy_num, l:phy_num_grace,
   --			l:phy_num_soft, l:phy_num_hard
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      offset = parse_xdr(tvb, item, "l", offset, "flags")
      offset = parse_xdr(tvb, item, "l", offset, "grace_period")
      offset = parse_xdr(tvb, item, "l", offset, "space")
      offset = parse_xdr(tvb, item, "l", offset, "space_grace")
      offset = parse_xdr(tvb, item, "l", offset, "space_soft")
      offset = parse_xdr(tvb, item, "l", offset, "space_hard")
      offset = parse_xdr(tvb, item, "l", offset, "num")
      offset = parse_xdr(tvb, item, "l", offset, "num_grace")
      offset = parse_xdr(tvb, item, "l", offset, "num_soft")
      offset = parse_xdr(tvb, item, "l", offset, "num_hard")
      offset = parse_xdr(tvb, item, "l", offset, "phy_space")
      offset = parse_xdr(tvb, item, "l", offset, "phy_space_grace")
      offset = parse_xdr(tvb, item, "l", offset, "phy_space_soft")
      offset = parse_xdr(tvb, item, "l", offset, "phy_space_hard")
      offset = parse_xdr(tvb, item, "l", offset, "phy_num")
      offset = parse_xdr(tvb, item, "l", offset, "phy_num_grace")
      offset = parse_xdr(tvb, item, "l", offset, "phy_num_soft")
      offset = parse_xdr(tvb, item, "l", offset, "phy_num_hard")
   end
   return offset
end

--
-- Parse GFM_PROTO_QUOTA_DIRSET_SET.
--
function parse_gfm_quota_dirset_set_request(tvb, pinfo, item, offset)
   -- IN s:username, s:dirsetname
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "username")
   offset = parse_xdr(tvb, item, "s", offset, "dirsetname")
   offset = parse_xdr(tvb, item, "l", offset, "grace_period")
   offset = parse_xdr(tvb, item, "l", offset, "space_soft")
   offset = parse_xdr(tvb, item, "l", offset, "space_hard")
   offset = parse_xdr(tvb, item, "l", offset, "num_soft")
   offset = parse_xdr(tvb, item, "l", offset, "num_hard")
   offset = parse_xdr(tvb, item, "l", offset, "phy_space_soft")
   offset = parse_xdr(tvb, item, "l", offset, "phy_space_hard")
   offset = parse_xdr(tvb, item, "l", offset, "phy_num_soft")
   offset = parse_xdr(tvb, item, "l", offset, "phy_num_hard")
   return offset
end

function parse_gfm_quota_dirset_set_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_QUOTA_DIR_GET.
--
function parse_gfm_quota_dir_get_request(tvb, pinfo, item, offset)
   -- IN (none)
   return offset + 4
end

function parse_gfm_quota_dir_get_response(tvb, pinfo, item, offset)
   -- OUT i:error_code
   --     (upon success)
   --			l:flags
   --			s:username, s:dirsetname,
   --			l:grace_period, l:space, l:space_grace,
   --			l:space_soft, l:space_hard, l:num, l:num_grace,
   --			l:num_soft, l:num_hard, l:phy_space,
   --			l:phy_space_grace, l:phy_space_soft,
   --			l:phy_space_hard, l:phy_num, l:phy_num_grace,
   --			l:phy_num_soft, l:phy_num_hard
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      offset = parse_xdr(tvb, item, "l", offset, "flags")
      offset = parse_xdr(tvb, item, "l", offset, "grace_period")
      offset = parse_xdr(tvb, item, "l", offset, "space")
      offset = parse_xdr(tvb, item, "l", offset, "space_grace")
      offset = parse_xdr(tvb, item, "l", offset, "space_soft")
      offset = parse_xdr(tvb, item, "l", offset, "space_hard")
      offset = parse_xdr(tvb, item, "l", offset, "num")
      offset = parse_xdr(tvb, item, "l", offset, "num_grace")
      offset = parse_xdr(tvb, item, "l", offset, "num_soft")
      offset = parse_xdr(tvb, item, "l", offset, "num_hard")
      offset = parse_xdr(tvb, item, "l", offset, "phy_space")
      offset = parse_xdr(tvb, item, "l", offset, "phy_space_grace")
      offset = parse_xdr(tvb, item, "l", offset, "phy_space_soft")
      offset = parse_xdr(tvb, item, "l", offset, "phy_space_hard")
      offset = parse_xdr(tvb, item, "l", offset, "phy_num")
      offset = parse_xdr(tvb, item, "l", offset, "phy_num_grace")
      offset = parse_xdr(tvb, item, "l", offset, "phy_num_soft")
      offset = parse_xdr(tvb, item, "l", offset, "phy_num_hard")
   end
   return offset
end

--
-- Parse GFM_PROTO_QUOTA_DIR_SET.
--
function parse_gfm_quota_dir_set_request(tvb, pinfo, item, offset)
   -- IN s:username, s:dirsetname
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "username")
   offset = parse_xdr(tvb, item, "s", offset, "dirsetname")
   return offset
end

function parse_gfm_quota_dir_set_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_DIRSET_DIR_LIST.
--
function parse_gfm_dirset_dir_list_request(tvb, pinfo, item, offset)
   -- IN s:username, s:dirsetname
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "username")
   offset = parse_xdr(tvb, item, "s", offset, "dirsetname")
   return offset
end

function parse_gfm_dirset_dir_list_response(tvb, pinfo, item, offset)
   -- OUT i:error_code
   --     (upon success) i:n_dirs,
   --			 i:error_code[n_dir]
   --                    (upon success)
   --			 		s[n_dir]:username
   --					s[n_dir]:dirsetname
   --					s[n_dir]:pathname
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      local n_dirsets
      offset, n_dirsets = parse_xdr(tvb, item, "i", offset, "n_dirsets")
      for i = 1, n_dirsets do
         local n_dirs
         offset = parse_xdr(tvb, item, "s", offset, "username")
         offset = parse_xdr(tvb, item, "s", offset, "dirsetname")
         offset, n_dirs = parse_xdr(tvb, item, "i", offset, "n_dirs")
	 for j = 1, n_dirs do

            offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
            if err == 0 then
               offset = parse_xdr(tvb, item, "s", offset, "pathname")
	    end
         end
      end
   end
   return offset
end

--
-- Parse GFM_PROTO_COMPOUND_BEGIN.
--
function parse_gfm_compound_begin_request(tvb, pinfo, item, offset)
   -- IN (none)
   return offset + 4
end

function parse_gfm_compound_begin_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_COMPOUND_END.
--
function parse_gfm_compound_end_request(tvb, pinfo, item, offset)
   -- IN (none)
   return offset + 4
end

function parse_gfm_compound_end_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_COMPOUND_ON_ERROR.
--
function parse_gfm_compound_on_error_request(tvb, pinfo, item, offset)
   -- IN i:error_code
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   return offset
end

function parse_gfm_compound_on_error_response(tvb, pinfo, item, offset)
   -- OUT (none)
   offset = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   return offset
end

--
-- Parse GFM_PROTO_PUT_FD.
--
function parse_gfm_put_fd_request(tvb, pinfo, item, offset)
   -- IN i:fd
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "fd")
   return offset
end

function parse_gfm_put_fd_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_GET_FD.
--
function parse_gfm_get_fd_request(tvb, pinfo, item, offset)
   -- IN (none)
   return offset + 4
end

function parse_gfm_get_fd_response(tvb, pinfo, item, offset)
   -- OUT error_code
   --     (upon success) i:fd
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      offset = parse_xdr(tvb, item, "i", offset, "fd")
   end
   return offset
end

--
-- Parse GFM_PROTO_SAVE_FD.
--
function parse_gfm_save_fd_request(tvb, pinfo, item, offset)
   -- IN (none)
   return offset + 4
end

function parse_gfm_save_fd_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_RESTORE_FD.
--
function parse_gfm_restore_fd_request(tvb, pinfo, item, offset)
   -- IN (none)
   return offset + 4
end

function parse_gfm_restore_fd_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_BEQUEATH_FD.
--
function parse_gfm_bequeath_fd_request(tvb, pinfo, item, offset)
   -- IN (none)
   return offset + 4
end

function parse_gfm_bequeath_fd_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_INHERIT_FD.
--
function parse_gfm_inherit_fd_request(tvb, pinfo, item, offset)
   -- IN i:fd
   return parse_gfm_put_fd_request(tvb, pinfo, item, offset)
end

function parse_gfm_inherit_fd_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_OPEN_ROOT.
--
function parse_gfm_open_root_request(tvb, pinfo, item, offset)
   -- IN i:flags
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "flags")
   return offset
end

function parse_gfm_open_root_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_OPEN_PARENT.
--
function parse_gfm_open_parent_request(tvb, pinfo, item, offset)
   -- IN i:flags
   return parse_gfm_open_root_request(tvb, pinfo, item, offset)
end

function parse_gfm_open_parent_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_OPEN.
--
function parse_gfm_open_request(tvb, pinfo, item, offset)
   -- IN s:name, i:flags
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "name")
   offset = parse_xdr(tvb, item, "i", offset, "flags")
   return offset
end

function parse_gfm_open_response(tvb, pinfo, item, offset)
   -- OUT error_code
   --     (upon success) l:inode_number, l:generation, i:mode
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      offset = parse_xdr(tvb, item, "l", offset, "i_node_number")
      offset = parse_xdr(tvb, item, "l", offset, "generation")
      offset = parse_xdr(tvb, item, "i", offset, "mode", base.OCT)
   end
   return offset
end

--
-- Parse GFM_PROTO_CREATE.
--
function parse_gfm_create_request(tvb, pinfo, item, offset)
   -- IN s:name, i:flags, i:mode
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "name")
   offset = parse_xdr(tvb, item, "i", offset, "flags")
   offset = parse_xdr(tvb, item, "i", offset, "mode", base.OCT)
   return offset
end

function parse_gfm_create_response(tvb, pinfo, item, offset)
   return parse_gfm_open_response(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_CLOSE.
--
function parse_gfm_close_request(tvb, pinfo, item, offset)
   -- IN (none)
   return offset + 4
end

function parse_gfm_close_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_VERIFY_TYPE.
--
function parse_gfm_verify_type_request(tvb, pinfo, item, offset)
   -- IN i:type
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "type")
   return offset
end

function parse_gfm_verify_type_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_VERIFY_TYPE_NOT.
--
function parse_gfm_verify_type_not_request(tvb, pinfo, item, offset)
   -- IN i:type
   return parse_gfm_verify_type_request(tvb, pinfo, item, offset)
end

function parse_gfm_verify_type_not_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_REVOKE_GFSD_ACCESS.
--
function parse_gfm_revoke_gfsd_access_request(tvb, pinfo, item, offset)
   -- IN i:fd
   return parse_gfm_put_fd_request(tvb, pinfo, item, offset)
end

function parse_gfm_revoke_gfsd_access_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_FSTAT.
--
function parse_gfm_fstat_request(tvb, pinfo, item, offset)
   -- IN (none)
   return offset + 4
end

function parse_gfm_fstat_response(tvb, pinfo, item, offset)
   -- OUT l:ino, l:gen, i:mode, l:nlink,
   -- s:user, s:group, l:size, l:ncopy,
   -- l:atime_sec, i:atime_nsec, 
   -- l:mtime_sec, i:mtime_nsec,
   -- l:ctime_sec, i:ctime_nsec
   offset = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   offset = parse_xdr(tvb, item, "l", offset, "i_node_number")
   offset = parse_xdr(tvb, item, "l", offset, "generation")
   offset = parse_xdr(tvb, item, "i", offset, "mode", base.OCT)
   offset = parse_xdr(tvb, item, "l", offset, "nlink")
   offset = parse_xdr(tvb, item, "s", offset, "user")
   offset = parse_xdr(tvb, item, "s", offset, "group")
   offset = parse_xdr(tvb, item, "l", offset, "size")
   offset = parse_xdr(tvb, item, "l", offset, "ncopy")
   offset = parse_xdr(tvb, item, "l", offset, "atime_sec", format_datetime)
   offset = parse_xdr(tvb, item, "i", offset, "atime_nsec")
   offset = parse_xdr(tvb, item, "l", offset, "mtime_sec", format_datetime)
   offset = parse_xdr(tvb, item, "i", offset, "mtime_nsec")
   offset = parse_xdr(tvb, item, "l", offset, "ctime_sec", format_datetime)
   offset = parse_xdr(tvb, item, "i", offset, "ctime_nsec")
   return offset
end

--
-- Parse GFM_PROTO_FUTIMES.
--
function parse_gfm_futimes_request(tvb, pinfo, item, offset)
   -- IN l:atime_sec, i:atime_nsec,
   --    l:mtime_sec, i:mtime_nsec
   offset = offset + 4
   offset = parse_xdr(tvb, item, "l", offset, "atime_sec", format_datetime)
   offset = parse_xdr(tvb, item, "i", offset, "atime_nsec")
   offset = parse_xdr(tvb, item, "l", offset, "mtime_sec", format_datetime)
   offset = parse_xdr(tvb, item, "i", offset, "mtime_nsec")
   return offset
end

function parse_gfm_futimes_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_FCHMOD.
--
function parse_gfm_fchmod_request(tvb, pinfo, item, offset)
   -- IN i:mode
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "mode", base.OCT)
   return offset
end

function parse_gfm_fchmod_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_FCHOWN.
--
function parse_gfm_fchown_request(tvb, pinfo, item, offset)
   -- IN s:user, s:group
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "user")
   offset = parse_xdr(tvb, item, "s", offset, "group")
   return offset
end

function parse_gfm_fchown_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_CKSUM_GET.
--
function parse_gfm_cksum_get_request(tvb, pinfo, item, offset)
end

function parse_gfm_cksum_get_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_CKSUM_SET.
--
function parse_gfm_cksum_set_request(tvb, pinfo, item, offset)
end

function parse_gfm_cksum_set_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_SCHEDULE_FILE.
--
function parse_gfm_schedule_file_request(tvb, pinfo, item, offset)
   -- IN s:domain
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "domain")
   return offset
end

function parse_gfm_schedule_file_response(tvb, pinfo, item, offset)
   -- OUT i:error_code
   --     (upon success) i:n_hosts,
   --                    s[n_host]:host, i[n_host]:port,
   --                    i[n_host]:loadavg*65536, l[n_host]:cache_time, 
   --                    l[n_host]:usedsize, l[n_host]:availsize,
   --                    l[n_host]:rtt_cache_time, i[n_host]:rtt_usec,
   --                    i[n_host]:rtt_flags
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      local n_hosts
      offset, n_hosts = parse_xdr(tvb, item, "i", offset, "n_hosts")
      for i = 1, n_hosts do
         offset = parse_xdr(tvb, item, "s", offset, "host")
         offset = parse_xdr(tvb, item, "i", offset, "port")
         offset = parse_xdr(tvb, item, "i", offset, "ncpu")
         offset = parse_xdr(tvb, item, "i", offset, "loadavg*65536")
         offset = parse_xdr(tvb, item, "l", offset, "cache_time")
         offset = parse_xdr(tvb, item, "l", offset, "usedsize")
         offset = parse_xdr(tvb, item, "l", offset, "availsize")
         offset = parse_xdr(tvb, item, "l", offset, "rtt_cache_time")
         offset = parse_xdr(tvb, item, "i", offset, "rtt_usec")
         offset = parse_xdr(tvb, item, "i", offset, "rtt_flags",
			    format_gfm_sched_flags)
      end
   end
   return offset
end

--
-- Parse GFM_PROTO_SCHEDULE_FILE_WITH_PROGRAM.
--
function parse_gfm_schedule_file_with_program_request(tvb, pinfo, item, offset)
   return parse_gfm_schedule_file_request(tvb, pinfo, item, offset)
end

function parse_gfm_schedule_file_with_program_response(tvb, pinfo, item, offset)
   return parse_gfm_schedule_file_response(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_FGETATTRPLUS.
--
function parse_gfm_fgetattrplus_request(tvb, pinfo, item, offset)
end

function parse_gfm_fgetattrplus_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_REMOVE.
--
function parse_gfm_remove_request(tvb, pinfo, item, offset)
   -- IN s:target
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "target")
   return offset
end

function parse_gfm_remove_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_RENAME.
--
function parse_gfm_rename_request(tvb, pinfo, item, offset)
   -- IN s:src_name, s: target_name
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "src_name")
   offset = parse_xdr(tvb, item, "s", offset, "target_name")
   return offset
end

function parse_gfm_rename_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_FLINK.
--
function parse_gfm_flink_request(tvb, pinfo, item, offset)
   -- IN s:target_name
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "target_name")
   return offset
end

function parse_gfm_flink_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_MKDIR.
--
function parse_gfm_mkdir_request(tvb, pinfo, item, offset)
   -- IN s:name, i:mode
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "target_name")
   offset = parse_xdr(tvb, item, "i", offset, "mode", base.OCT)
   return offset
end

function parse_gfm_mkdir_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_SYMLINK.
--
function parse_gfm_symlink_request(tvb, pinfo, item, offset)
   -- IN s:source_path, s:new_name
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "source_path")
   offset = parse_xdr(tvb, item, "s", offset, "new_name")
   return offset
end

function parse_gfm_symlink_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_READLINK.
--
function parse_gfm_readlink_request(tvb, pinfo, item, offset)
   -- IN (none)
   return offset + 4
end

function parse_gfm_readlink_response(tvb, pinfo, item, offset)
   offset = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   offset = parse_xdr(tvb, item, "s", offset, "name")
   return offset
end

--
-- Parse GFM_PROTO_GETDIRPATH.
--
function parse_gfm_getdirpath_request(tvb, pinfo, item, offset)
   -- IN (none)
   return offset + 4
end

function parse_gfm_getdirpath_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_GETDIRENTS.
--
function parse_gfm_getdirents_request(tvb, pinfo, item, offset)
   -- IN i:n_entries
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "n_entries")
   return offset
end

function parse_gfm_getdirents_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_SEEK.
--
function parse_gfm_seek_request(tvb, pinfo, item, offset)
   -- IN l:offset, i:whence
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "offset")
   offset = parse_xdr(tvb, item, "i", offset, "whence", whence_names)
   return offset
end

function parse_gfm_seek_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_GETDIRENTSPLUS.
--
function parse_gfm_getdirentsplus_request(tvb, pinfo, item, offset)
end

function parse_gfm_getdirentsplus_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_GETDIRENTSPLUSXATTR.
--
function parse_gfm_getdirentsplusxattr_request(tvb, pinfo, item, offset)
  -- IN i:n_entries, i:n_patterns, s[npatterns]:patterns
   offset = parse_xdr(tvb, item, "i", offset, "n_entries")
   offset, n_patterns = parse_xdr(tvb, item, "i", offset, "n_patterns")
   for i = 1, n_patterns do
      offset = parse_xdr(tvb, item, "s", offset, "patterns")
   end
   return offset
end

function parse_gfm_getdirentsplusxattr_response(tvb, pinfo, item, offset)
   -- OUT s:name, l:ino, l:gen, i:mode, l:nlink,
   -- s:user, s:group, l:size, l:ncopy,
   -- l:atime_sec, i:atime_nsec, 
   -- l:mtime_sec, i:mtime_nsec,
   -- l:ctime_sec, i:ctime_nsec, 
   -- i:nxattrs, s[0]:key, B[0]:value, ...
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      local n_stats
      local n_xattr
      offset, n_stats = parse_xdr(tvb, item, "i", offset, "n_stats")
      for i = 1, n_stats do
         offset = parse_xdr(tvb, item, "s", offset, "name")
         offset = parse_xdr(tvb, item, "l", offset, "i_node_number")
         offset = parse_xdr(tvb, item, "l", offset, "generation")
         offset = parse_xdr(tvb, item, "i", offset, "mode", base.OCT)
         offset = parse_xdr(tvb, item, "l", offset, "nlink")
         offset = parse_xdr(tvb, item, "s", offset, "user")
         offset = parse_xdr(tvb, item, "s", offset, "group")
         offset = parse_xdr(tvb, item, "l", offset, "size")
         offset = parse_xdr(tvb, item, "l", offset, "ncopy")
         offset = parse_xdr(tvb, item, "l", offset, "atime_sec", format_datetime)
         offset = parse_xdr(tvb, item, "i", offset, "atime_nsec")
         offset = parse_xdr(tvb, item, "l", offset, "mtime_sec", format_datetime)
         offset = parse_xdr(tvb, item, "i", offset, "mtime_nsec")
         offset = parse_xdr(tvb, item, "l", offset, "ctime_sec", format_datetime)
         offset = parse_xdr(tvb, item, "i", offset, "ctime_nsec")
         offset, n_xattr = parse_xdr(tvb, item, "i", offset, "nxattr")
         for j = 1, n_xattr do
            offset = parse_xdr(tvb, item, "s", offset, "name")
            offset = parse_xdr(tvb, item, "b", offset, "value")
         end
      end
   end
   return offset
end

--
-- Parse GFM_PROTO_REOPEN.
--
function parse_gfm_reopen_request(tvb, pinfo, item, offset)
   -- IN (none)
   return offset + 4
end

function parse_gfm_reopen_response(tvb, pinfo, item, offset)
   -- OUT error_code
   --     (upon success) l:inode_number, l:generation, i:mode,
   --                    i:flags, i:to_create
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      offset = parse_xdr(tvb, item, "l", offset, "inode_number")
      offset = parse_xdr(tvb, item, "l", offset, "generation")
      offset = parse_xdr(tvb, item, "i", offset, "mode", base.OCT)
      offset = parse_xdr(tvb, item, "i", offset, "flags")
      offset = parse_xdr(tvb, item, "i", offset, "to_create")
   end
   return offset
end

--
-- Parse GFM_PROTO_CLOSE_READ.
--
function parse_gfm_close_read_request(tvb, pinfo, item, offset)
   -- IN l:atime_sec, i:atime_nsec
   offset = offset + 4
   offset = parse_xdr(tvb, item, "l", offset, "atime_sec", format_datetime)
   offset = parse_xdr(tvb, item, "i", offset, "atime_nsec")
   return offset
end

function parse_gfm_close_read_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_CLOSE_WRITE.
--
function parse_gfm_close_write_request(tvb, pinfo, item, offset)
   -- IN l:size
   --    l:atime_sec, i:atime_nsec
   --    l:mtime_sec, i:mtime_nsec
   offset = offset + 4
   offset = parse_xdr(tvb, item, "l", offset, "atime_sec", format_datetime)
   offset = parse_xdr(tvb, item, "i", offset, "atime_nsec")
   offset = parse_xdr(tvb, item, "l", offset, "mtime_sec", format_datetime)
   offset = parse_xdr(tvb, item, "i", offset, "mtime_nsec")
   return offset
end

function parse_gfm_close_write_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_LOCK.
--
function parse_gfm_lock_request(tvb, pinfo, item, offset)
   -- IN l:start, l:len, i:type, i:whence
   offset = offset + 4
   offset = parse_xdr(tvb, item, "l", offset, "start")
   offset = parse_xdr(tvb, item, "l", offset, "len")
   offset = parse_xdr(tvb, item, "i", offset, "type")
   offset = parse_xdr(tvb, item, "i", offset, "whence", whence_names)
   return offset
end

function parse_gfm_lock_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_TRYLOCK.
--
function parse_gfm_trylock_request(tvb, pinfo, item, offset)
   -- IN l:start, l:len, i:type, i:whence
   return parse_gfm_lock_request(tvb, pinfo, item, offset)
end

function parse_gfm_trylock_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_UNLOCK.
--
function parse_gfm_unlock_request(tvb, pinfo, item, offset)
   -- IN l:start, l:len, i:type, i:whence
   return parse_gfm_lock_request(tvb, pinfo, item, offset)
end

function parse_gfm_unlock_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_LOCK_INFO.
--
function parse_gfm_lock_info_request(tvb, pinfo, item, offset)
   -- IN l:start, l:len, i:type, i:whence
   return parse_gfm_lock_request(tvb, pinfo, item, offset)
end

function parse_gfm_lock_info_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_SWITCH_BACK_CHANNEL.
--
function parse_gfm_switch_back_channel_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_switch_back_channel_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_SWITCH_ASYNC_BACK_CHANNEL.
--
function parse_gfm_switch_async_back_channel_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_switch_async_back_channel_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_CLOSE_WRITE_V2_4.
--
function parse_gfm_close_write_v2_4_request(tvb, pinfo, item, offset)
   -- IN l:size
   --    l:atime_sec, i:atime_nsec
   --    l:mtime_sec, i:mtime_nsec
   offset = offset + 4
   offset = parse_xdr(tvb, item, "l", offset, "size")
   offset = parse_xdr(tvb, item, "l", offset, "atime_sec", format_datetime)
   offset = parse_xdr(tvb, item, "i", offset, "atime_nsec")
   offset = parse_xdr(tvb, item, "l", offset, "mtime_sec", format_datetime)
   offset = parse_xdr(tvb, item, "i", offset, "mtime_nsec")
   return offset
end

function parse_gfm_close_write_v2_4_response(tvb, pinfo, item, offset)
   -- OUT error_code
   --     (upon success) i:flags, l:old_gen, l:new_gen
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      offset = parse_xdr(tvb, item, "i", offset, "flags",
			 format_gfm_write_v2_4_flags)
      offset = parse_xdr(tvb, item, "l", offset, "old_gen")
      offset = parse_xdr(tvb, item, "l", offset, "new_gen")
   end
   return offset
end

--
-- Parse GFM_PROTO_GENERATION_UPDATED.
--
function parse_gfm_generation_updated_request(tvb, pinfo, item, offset)
   -- IN i:error_code
   return parse_gfm_compound_on_error_request(tvb, pinfo, item, offset)
end

function parse_gfm_generation_updated_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_FHCLOSE_READ.
--
function parse_gfm_fhclose_read_request(tvb, pinfo, item, offset)
   -- IN l:inode_number, l:gen, l:atime_sec, i:atime_nsec
   offset = offset + 4
   offset = parse_xdr(tvb, item, "l", offset, "inode_number")
   offset = parse_xdr(tvb, item, "l", offset, "gen")
   offset = parse_xdr(tvb, item, "l", offset, "atime_sec", format_datetime)
   offset = parse_xdr(tvb, item, "i", offset, "atime_nsec")
   return offset
end

function parse_gfm_fhclose_read_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_FHCLOSE_WRITE.
--
function parse_gfm_fhclose_write_request(tvb, pinfo, item, offset)
   -- IN l:inode_number, l:gen, l:size
   --    l:atime_sec, i:atime_nsec
   --    l:mtime_sec, i:mtime_nsec
   offset = offset + 4
   offset = parse_xdr(tvb, item, "l", offset, "inode_number")
   offset = parse_xdr(tvb, item, "l", offset, "gen")
   offset = parse_xdr(tvb, item, "l", offset, "size")
   offset = parse_xdr(tvb, item, "l", offset, "atime_sec", format_datetime)
   offset = parse_xdr(tvb, item, "i", offset, "atime_nsec")
   offset = parse_xdr(tvb, item, "l", offset, "mtime_sec", format_datetime)
   offset = parse_xdr(tvb, item, "i", offset, "mtime_nsec")
   return offset
end

function parse_gfm_fhclose_write_response(tvb, pinfo, item, offset)
   -- OUT error_code
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      offset = parse_xdr(tvb, item, "i", offset, "flags",
			 format_gfm_write_v2_4_flags)
      offset = parse_xdr(tvb, item, "l", offset, "old_gen")
      offset = parse_xdr(tvb, item, "l", offset, "new_gen")
      offset = parse_xdr(tvb, item, "l", offset, "cookie")
   end
   return offset
end

--
-- Parse GFM_PROTO_GENERATION_UPDATED_BY_COOKIE.
--
function parse_gfm_generation_updated_by_cookie_request(tvb, pinfo, item, offset)
   -- IN l:cookie, i:errcode
   offset = offset + 4
   offset = parse_xdr(tvb, item, "l", offset, "cookie")
   offset = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   return offset
end

function parse_gfm_generation_updated_by_cookie_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_GLOB.
--
function parse_gfm_glob_request(tvb, pinfo, item, offset)
   -- IN i:n_globs, s[n_globs]:globs
   return nil
end

function parse_gfm_glob_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_SCHEDULE.
--
function parse_gfm_schedule_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_schedule_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_PIO_OPEN.
--
function parse_gfm_pio_open_request(tvb, pinfo, item, offset)
   -- IN s:parallel_file, i:open_flags
   offset = offset + 4
   local n_globs
   offset, n_globs = parse_xdr(tvb, item, "s", offset, "parallel_file")
   offset = parse_xdr(tvb, item, "i", offset, "open_flags", base.HEX)
   return offset
end

function parse_gfm_pio_open_response(tvb, pinfo, item, offset)
   -- OUT error_code
   --     (upon success) i:n_files, s[n_files]:filenames
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      local n_files
      offset, n_files = parse_xdr(tvb, item, "i", offset, "n_files")
      for i = 1, n_files do
         offset = parse_xdr(tvb, item, "s", offset, "filename")
      end
   end
   return offset
end

--
-- Parse GFM_PROTO_PIO_SET_PATHS.
--
function parse_gfm_pio_set_paths_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_pio_set_paths_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_PIO_CLOSE.
--
function parse_gfm_pio_close_request(tvb, pinfo, item, offset)
   -- IN (none)
   return offset + 4
end

function parse_gfm_pio_close_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_PIO_VISIT.
--
function parse_gfm_pio_visit_request(tvb, pinfo, item, offset)
   -- IN s:parallel_file, i:open_flags
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "index")
   return offset
end

function parse_gfm_pio_visit_response(tvb, pinfo, item, offset)
   -- OUT error_code
   --     (upon success) i:flags
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      offset = parse_xdr(tvb, item, "i", offset, "flags")
   end
   return offset
end

--
-- Parse GFM_PROTO_HOSTNAME_SET.
--
function parse_gfm_hostname_set_request(tvb, pinfo, item, offset)
   -- IN s:hostname (undocumented)
   offset = offset + 4
   offset = parse_xdr(tvb, item, "s", offset, "hostname")
   return offset
end

function parse_gfm_hostname_set_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_SCHEDULE_HOST_DOMAIN.
--
function parse_gfm_schedule_host_domain_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_schedule_host_domain_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_STATFS.
--
function parse_gfm_statfs_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_statfs_response(tvb, pinfo, item, offset)
   -- OUT error_code
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      offset = parse_xdr(tvb, item, "l", offset, "used")
      offset = parse_xdr(tvb, item, "l", offset, "avail")
      offset = parse_xdr(tvb, item, "l", offset, "files")
   end
   return offset
end

--
-- Parse GFM_PROTO_REPLICA_LIST_BY_NAME.
--
function parse_gfm_replica_list_by_name_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_replica_list_by_name_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_REPLICA_LIST_BY_HOST.
--
function parse_gfm_replica_list_by_host_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_replica_list_by_host_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_REPLICA_REMOVE_BY_HOST.
--
function parse_gfm_replica_remove_by_host_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_replica_remove_by_host_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_REPLICA_REMOVE_BY_FILE.
--
function parse_gfm_replica_remove_by_file_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_replica_remove_by_file_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_REPLICA_INFO_GET.
--
function parse_gfm_replica_info_get_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_replica_info_get_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_REPLICATE_FILE_FROM_TO.
--
function parse_gfm_replicate_file_from_to_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_replicate_file_from_to_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_REPLICATE_FILE_TO.
--
function parse_gfm_replicate_file_to_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_replicate_file_to_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_REPLICA_ADDING.
--
function parse_gfm_replica_adding_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_replica_adding_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_REPLICA_ADDED.
--
function parse_gfm_replica_added_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_replica_added_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_REPLICA_LOST.
--
function parse_gfm_replica_lost_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_replica_lost_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_REPLICA_ADD.
--
function parse_gfm_replica_add_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_replica_add_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_REPLICA_ADDED2.
--
function parse_gfm_replica_added2_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_replica_added2_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_REPLICATION_RESULT.
--
function parse_gfm_replication_result_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_replication_result_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_PROCESS_ALLOC.
--
function parse_gfm_process_alloc_request(tvb, pinfo, item, offset)
   -- IN i:key_type, b:shared_key
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "keytype")
   offset = parse_xdr(tvb, item, "b", offset, "shared_key")
   return offset
end

function parse_gfm_process_alloc_response(tvb, pinfo, item, offset)
   -- OUT l:pid
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      offset = parse_xdr(tvb, item, "l", offset, "pid", base.DEC)
   end
   return nil
end

--
-- Parse GFM_PROTO_PROCESS_ALLOC_CHILD.
--
function parse_gfm_process_alloc_child_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_process_alloc_child_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_PROCESS_FREE.
--
function parse_gfm_process_free_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_process_free_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFM_PROTO_PROCESS_SET.
--
function parse_gfm_process_set_request(tvb, pinfo, item, offset)
   -- IN i:key_type, b:shared_key, l:pid
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "cookie")
   offset = parse_xdr(tvb, item, "b", offset, "shared_key")
   offset = parse_xdr(tvb, item, "l", offset, "pid", base.DEC)
   return offset
end

function parse_gfm_process_set_response(tvb, pinfo, item, offset)
   -- OUT error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFJ_PROTO_LOCK_REGISTER.
--
function parse_gfj_lock_register_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfj_lock_register_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFJ_PROTO_UNLOCK_REGISTER.
--
function parse_gfj_unlock_register_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfj_unlock_register_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFJ_PROTO_REGISTER.
--
function parse_gfj_register_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfj_register_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFJ_PROTO_UNREGISTER.
--
function parse_gfj_unregister_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfj_unregister_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFJ_PROTO_REGISTER_NODE.
--
function parse_gfj_register_node_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfj_register_node_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFJ_PROTO_LIST.
--
function parse_gfj_list_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfj_list_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFJ_PROTO_INFO.
--
function parse_gfj_info_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfj_info_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFJ_PROTO_HOSTINFO.
--
function parse_gfj_hostinfo_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfj_hostinfo_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_XATTR_SET.
--
function parse_gfm_xattr_set_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_xattr_set_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_XMLATTR_SET.
--
function parse_gfm_xmlattr_set_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_xmlattr_set_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_XATTR_GET.
--
function parse_gfm_xattr_get_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_xattr_get_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_XMLATTR_GET.
--
function parse_gfm_xmlattr_get_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_xmlattr_get_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_XATTR_REMOVE.
--
function parse_gfm_xattr_remove_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_xattr_remove_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_XMLATTR_REMOVE.
--
function parse_gfm_xmlattr_remove_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_xmlattr_remove_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_XATTR_LIST.
--
function parse_gfm_xattr_list_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_xattr_list_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_XMLATTR_LIST.
--
function parse_gfm_xmlattr_list_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_xmlattr_list_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_XMLATTR_FIND.
--
function parse_gfm_xmlattr_find_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_xmlattr_find_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_SWITCH_GFMD_CHANNEL.
--
function parse_gfm_switch_gfmd_channel_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_switch_gfmd_channel_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_JOURNAL_READY_TO_RECV.
--
function parse_gfm_journal_ready_to_recv_request(tvb, pinfo, item, offset)
end

function parse_gfm_journal_ready_to_recv_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_JOURNAL_SEND.
--
function parse_gfm_journal_send_request(tvb, pinfo, item, offset)
end

function parse_gfm_journal_send_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_METADB_SERVER_GET.
--
function parse_gfm_metadb_server_get_request(tvb, pinfo, item, offset)
end

function parse_gfm_metadb_server_get_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_METADB_SERVER_GET_ALL.
--
function parse_gfm_metadb_server_get_all_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_metadb_server_get_all_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_METADB_SERVER_SET.
--
function parse_gfm_metadb_server_set_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_metadb_server_set_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_METADB_SERVER_MODIFY.
--
function parse_gfm_metadb_server_modify_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_metadb_server_modify_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse GFM_PROTO_METADB_SERVER_REMOVE.
--
function parse_gfm_metadb_server_remove_request(tvb, pinfo, item, offset)
   return nil
end

function parse_gfm_metadb_server_remove_response(tvb, pinfo, item, offset)
   return nil
end

--
-- Parse function for each command.
--
local gfm_command_parsers = {
   [  0] = {request  = parse_gfm_host_info_get_all_request,
            response = parse_gfm_host_info_get_all_response},
   [  1] = {request  = parse_gfm_host_info_get_by_architecture_request,
            response = parse_gfm_host_info_get_by_architecture_response},
   [  2] = {request  = parse_gfm_host_info_get_by_names_request,
            response = parse_gfm_host_info_get_by_names_response},
   [  3] = {request  = parse_gfm_host_info_get_by_namealiases_request,
            response = parse_gfm_host_info_get_by_namealiases_response},
   [  4] = {request  = parse_gfm_host_info_set_request,
            response = parse_gfm_host_info_set_response},
   [  5] = {request  = parse_gfm_host_info_modify_request,
            response = parse_gfm_host_info_modify_response},
   [  6] = {request  = parse_gfm_host_info_remove_request,
            response = parse_gfm_host_info_remove_response},
   [ 16] = {request  = parse_gfm_user_info_get_all_request,
            response = parse_gfm_user_info_get_all_response},
   [ 17] = {request  = parse_gfm_user_info_get_by_names_request,
            response = parse_gfm_user_info_get_by_names_response},
   [ 18] = {request  = parse_gfm_user_info_set_request,
            response = parse_gfm_user_info_set_response},
   [ 19] = {request  = parse_gfm_user_info_modify_request,
            response = parse_gfm_user_info_modify_response},
   [ 20] = {request  = parse_gfm_user_info_remove_request,
            response = parse_gfm_user_info_remove_response},
   [ 21] = {request  = parse_gfm_user_info_get_by_gsi_dn_request,
            response = parse_gfm_user_info_get_by_gsi_dn_response},
   [ 32] = {request  = parse_gfm_group_info_get_all_request,
            response = parse_gfm_group_info_get_all_response},
   [ 33] = {request  = parse_gfm_group_info_get_by_names_request,
            response = parse_gfm_group_info_get_by_names_response},
   [ 34] = {request  = parse_gfm_group_info_set_request,
            response = parse_gfm_group_info_set_response},
   [ 35] = {request  = parse_gfm_group_info_modify_request,
            response = parse_gfm_group_info_modify_response},
   [ 36] = {request  = parse_gfm_group_info_remove_request,
            response = parse_gfm_group_info_remove_response},
   [ 37] = {request  = parse_gfm_group_info_add_users_request,
            response = parse_gfm_group_info_add_users_response},
   [ 38] = {request  = parse_gfm_group_info_remove_users_request,
            response = parse_gfm_group_info_remove_users_response},
   [ 39] = {request  = parse_gfm_group_names_get_by_users_request,
            response = parse_gfm_group_names_get_by_users_response},
   [ 48] = {request  = parse_gfm_quota_user_get_request,
            response = parse_gfm_quota_user_get_response},
   [ 49] = {request  = parse_gfm_quota_user_set_request,
            response = parse_gfm_quota_user_set_response},
   [ 50] = {request  = parse_gfm_quota_group_get_request,
            response = parse_gfm_quota_group_get_response},
   [ 51] = {request  = parse_gfm_quota_group_set_request,
            response = parse_gfm_quota_group_set_response},
   [ 52] = {request  = parse_gfm_quota_check_request,
            response = parse_gfm_quota_check_response},
   [ 54] = {request  = parse_gfm_dirset_info_set_request,
            response = parse_gfm_dirset_info_set_response},
   [ 55] = {request  = parse_gfm_dirset_info_remove_request,
            response = parse_gfm_dirset_info_remove_response},
   [ 56] = {request  = parse_gfm_dirset_info_list_request,
            response = parse_gfm_dirset_info_list_response},
   [ 57] = {request  = parse_gfm_quota_dirset_get_request,
            response = parse_gfm_quota_dirset_get_response},
   [ 58] = {request  = parse_gfm_quota_dirset_set_request,
            response = parse_gfm_quota_dirset_set_response},
   [ 59] = {request  = parse_gfm_quota_dir_get_request,
            response = parse_gfm_quota_dir_get_response},
   [ 60] = {request  = parse_gfm_quota_dir_set_request,
            response = parse_gfm_quota_dir_set_response},
   [ 61] = {request  = parse_gfm_dirset_dir_list_request,
            response = parse_gfm_dirset_dir_list_response},
   [ 64] = {request  = parse_gfm_compound_begin_request,
            response = parse_gfm_compound_begin_response},
   [ 65] = {request  = parse_gfm_compound_end_request,
            response = parse_gfm_compound_end_response},
   [ 66] = {request  = parse_gfm_compound_on_error_request,
            response = parse_gfm_compound_on_error_response},
   [ 67] = {request  = parse_gfm_put_fd_request,
            response = parse_gfm_put_fd_response},
   [ 68] = {request  = parse_gfm_get_fd_request,
            response = parse_gfm_get_fd_response},
   [ 69] = {request  = parse_gfm_save_fd_request,
            response = parse_gfm_save_fd_response},
   [ 70] = {request  = parse_gfm_restore_fd_request,
            response = parse_gfm_restore_fd_response},
   [ 71] = {request  = parse_gfm_bequeath_fd_request,
            response = parse_gfm_bequeath_fd_response},
   [ 72] = {request  = parse_gfm_inherit_fd_request,
            response = parse_gfm_inherit_fd_response},
   [ 80] = {request  = parse_gfm_open_root_request,
            response = parse_gfm_open_root_response},
   [ 81] = {request  = parse_gfm_open_parent_request,
            response = parse_gfm_open_parent_response},
   [ 82] = {request  = parse_gfm_open_request,
            response = parse_gfm_open_response},
   [ 83] = {request  = parse_gfm_create_request,
            response = parse_gfm_create_response},
   [ 84] = {request  = parse_gfm_close_request,
            response = parse_gfm_close_response},
   [ 85] = {request  = parse_gfm_verify_type_request,
            response = parse_gfm_verify_type_response},
   [ 86] = {request  = parse_gfm_verify_type_not_request,
            response = parse_gfm_verify_type_not_response},
   [ 87] = {request  = parse_gfm_revoke_gfsd_access_request,
            response = parse_gfm_revoke_gfsd_access_response},
   [ 96] = {request  = parse_gfm_fstat_request,
            response = parse_gfm_fstat_response},
   [ 97] = {request  = parse_gfm_futimes_request,
            response = parse_gfm_futimes_response},
   [ 98] = {request  = parse_gfm_fchmod_request,
            response = parse_gfm_fchmod_response},
   [ 99] = {request  = parse_gfm_fchown_request,
            response = parse_gfm_fchown_response},
   [100] = {request  = parse_gfm_cksum_get_request,
            response = parse_gfm_cksum_get_response},
   [101] = {request  = parse_gfm_cksum_set_request,
            response = parse_gfm_cksum_set_response},
   [102] = {request  = parse_gfm_schedule_file_request,
            response = parse_gfm_schedule_file_response},
   [103] = {request  = parse_gfm_schedule_file_with_program_request,
            response = parse_gfm_schedule_file_with_program_response},
   [104] = {request  = parse_gfm_fgetattrplus_request,
            response = parse_gfm_fgetattrplus_response},
   [112] = {request  = parse_gfm_remove_request,
            response = parse_gfm_remove_response},
   [113] = {request  = parse_gfm_rename_request,
            response = parse_gfm_rename_response},
   [114] = {request  = parse_gfm_flink_request,
            response = parse_gfm_flink_response},
   [115] = {request  = parse_gfm_mkdir_request,
            response = parse_gfm_mkdir_response},
   [116] = {request  = parse_gfm_symlink_request,
            response = parse_gfm_symlink_response},
   [117] = {request  = parse_gfm_readlink_request,
            response = parse_gfm_readlink_response},
   [118] = {request  = parse_gfm_getdirpath_request,
            response = parse_gfm_getdirpath_response},
   [119] = {request  = parse_gfm_getdirents_request,
            response = parse_gfm_getdirents_response},
   [120] = {request  = parse_gfm_seek_request,
            response = parse_gfm_seek_response},
   [121] = {request  = parse_gfm_getdirentsplus_request,
            response = parse_gfm_getdirentsplus_response},
   [122] = {request  = parse_gfm_getdirentsplusxattr_request,
            response = parse_gfm_getdirentsplusxattr_response},
   [128] = {request  = parse_gfm_reopen_request,
            response = parse_gfm_reopen_response},
   [129] = {request  = parse_gfm_close_read_request,
            response = parse_gfm_close_read_response},
   [130] = {request  = parse_gfm_close_write_request,
            response = parse_gfm_close_write_response},
   [131] = {request  = parse_gfm_lock_request,
            response = parse_gfm_lock_response},
   [132] = {request  = parse_gfm_trylock_request,
            response = parse_gfm_trylock_response},
   [133] = {request  = parse_gfm_unlock_request,
            response = parse_gfm_unlock_response},
   [134] = {request  = parse_gfm_lock_info_request,
            response = parse_gfm_lock_info_response},
   [135] = {request  = parse_gfm_switch_back_channel_request,
            response = parse_gfm_switch_back_channel_response},
   [136] = {request  = parse_gfm_switch_async_back_channel_request,
            response = parse_gfm_switch_async_back_channel_response},
   [137] = {request  = parse_gfm_close_write_v2_4_request,
            response = parse_gfm_close_write_v2_4_response},
   [138] = {request  = parse_gfm_generation_updated_request,
            response = parse_gfm_generation_updated_response},
   [139] = {request  = parse_gfm_fhclose_read_request,
            response = parse_gfm_fhclose_read_response},
   [140] = {request  = parse_gfm_fhclose_write_request,
            response = parse_gfm_fhclose_write_response},
   [141] = {request  = parse_gfm_generation_updated_by_cookie_request,
            response = parse_gfm_generation_updated_by_cookie_response},
   [144] = {request  = parse_gfm_glob_request,
            response = parse_gfm_glob_response},
   [145] = {request  = parse_gfm_schedule_request,
            response = parse_gfm_schedule_response},
   [146] = {request  = parse_gfm_pio_open_request,
            response = parse_gfm_pio_open_response},
   [147] = {request  = parse_gfm_pio_set_paths_request,
            response = parse_gfm_pio_set_paths_response},
   [148] = {request  = parse_gfm_pio_close_request,
            response = parse_gfm_pio_close_response},
   [149] = {request  = parse_gfm_pio_visit_request,
            response = parse_gfm_pio_visit_response},
   [176] = {request  = parse_gfm_hostname_set_request,
            response = parse_gfm_hostname_set_response},
   [177] = {request  = parse_gfm_schedule_host_domain_request,
            response = parse_gfm_schedule_host_domain_response},
   [178] = {request  = parse_gfm_statfs_request,
            response = parse_gfm_statfs_response},
   [192] = {request  = parse_gfm_replica_list_by_name_request,
            response = parse_gfm_replica_list_by_name_response},
   [193] = {request  = parse_gfm_replica_list_by_host_request,
            response = parse_gfm_replica_list_by_host_response},
   [194] = {request  = parse_gfm_replica_remove_by_host_request,
            response = parse_gfm_replica_remove_by_host_response},
   [195] = {request  = parse_gfm_replica_remove_by_file_request,
            response = parse_gfm_replica_remove_by_file_response},
   [196] = {request  = parse_gfm_replica_info_get_request,
            response = parse_gfm_replica_info_get_response},
   [197] = {request  = parse_gfm_replicate_file_from_to_request,
            response = parse_gfm_replicate_file_from_to_response},
   [198] = {request  = parse_gfm_replicate_file_to_request,
            response = parse_gfm_replicate_file_to_response},
   [208] = {request  = parse_gfm_replica_adding_request,
            response = parse_gfm_replica_adding_response},
   [209] = {request  = parse_gfm_replica_added_request,
            response = parse_gfm_replica_added_response},
   [210] = {request  = parse_gfm_replica_lost_request,
            response = parse_gfm_replica_lost_response},
   [211] = {request  = parse_gfm_replica_add_request,
            response = parse_gfm_replica_add_response},
   [212] = {request  = parse_gfm_replica_added2_request,
            response = parse_gfm_replica_added2_response},
   [213] = {request  = parse_gfm_replication_result_request,
            response = parse_gfm_replication_result_response},
   [224] = {request  = parse_gfm_process_alloc_request,
            response = parse_gfm_process_alloc_response},
   [225] = {request  = parse_gfm_process_alloc_child_request,
            response = parse_gfm_process_alloc_child_response},
   [226] = {request  = parse_gfm_process_free_request,
            response = parse_gfm_process_free_response},
   [227] = {request  = parse_gfm_process_set_request,
            response = parse_gfm_process_set_response},
   [240] = {request  = parse_gfj_lock_register_request,
            response = parse_gfj_lock_register_response},
   [241] = {request  = parse_gfj_unlock_register_request,
            response = parse_gfj_unlock_register_response},
   [242] = {request  = parse_gfj_register_request,
            response = parse_gfj_register_response},
   [243] = {request  = parse_gfj_unregister_request,
            response = parse_gfj_unregister_response},
   [244] = {request  = parse_gfj_register_node_request,
            response = parse_gfj_register_node_response},
   [245] = {request  = parse_gfj_list_request,
            response = parse_gfj_list_response},
   [246] = {request  = parse_gfj_info_request,
            response = parse_gfj_info_response},
   [247] = {request  = parse_gfj_hostinfo_request,
            response = parse_gfj_hostinfo_response},
   [256] = {request  = parse_gfm_xattr_set_request,
            response = parse_gfm_xattr_set_response},
   [257] = {request  = parse_gfm_xmlattr_set_request,
            response = parse_gfm_xmlattr_set_response},
   [258] = {request  = parse_gfm_xattr_get_request,
            response = parse_gfm_xattr_get_response},
   [259] = {request  = parse_gfm_xmlattr_get_request,
            response = parse_gfm_xmlattr_get_response},
   [260] = {request  = parse_gfm_xattr_remove_request,
            response = parse_gfm_xattr_remove_response},
   [261] = {request  = parse_gfm_xmlattr_remove_request,
            response = parse_gfm_xmlattr_remove_response},
   [262] = {request  = parse_gfm_xattr_list_request,
            response = parse_gfm_xattr_list_response},
   [263] = {request  = parse_gfm_xmlattr_list_request,
            response = parse_gfm_xmlattr_list_response},
   [264] = {request  = parse_gfm_xmlattr_find_request,
            response = parse_gfm_xmlattr_find_response},
   [272] = {request  = parse_gfm_switch_gfmd_channel_request,
            response = parse_gfm_switch_gfmd_channel_response},
   [273] = {request  = parse_gfm_journal_ready_to_recv_request,
            response = parse_gfm_journal_ready_to_recv_response},
   [274] = {request  = parse_gfm_journal_send_request,
            response = parse_gfm_journal_send_response},
   [288] = {request  = parse_gfm_metadb_server_get_request,
            response = parse_gfm_metadb_server_get_response},
   [289] = {request  = parse_gfm_metadb_server_get_all_request,
            response = parse_gfm_metadb_server_get_all_response},
   [290] = {request  = parse_gfm_metadb_server_set_request,
            response = parse_gfm_metadb_server_set_response},
   [291] = {request  = parse_gfm_metadb_server_modify_request,
            response = parse_gfm_metadb_server_modify_response},
   [292] = {request  = parse_gfm_metadb_server_remove_request,
            response = parse_gfm_metadb_server_remove_response},
}

--
-- Packet number of a request packet that the dissector has previously
-- parsed.
--
local gfm_last_request_number = -1

--
-- Array to search a request packet number which corresponds
-- with a response packet number.
--
local gfm_response_to_request = {}

--
-- History of issued commands. 
--
local gfm_command_history = {}

--
-- Request dissector.
--
function gfm_request_dissector(tvb, pinfo, root, history_key)
   local pinfo_cmd_name = "unknown command"
   pinfo.cols.protocol = gfm_proto.name
   pinfo.cols.info = "Gfarm gfm request (" .. pinfo_cmd_name .. ")"
   pinfo.desegment_len = 0
   local item = root:add(gfm_proto, tvb(), "Gfarm gfm protocol (request)")
   gfm_command_history[history_key] = {}

   local i = 1
   local offset = pinfo.desegment_offset
   pinfo.desegment_offset = 0

   while offset < tvb():len() do
      if offset + 4 > tvb():len() then
         pinfo.desegment_len = DESEGMENT_ONE_MORE_SEGMENT
         pinfo.desegment_offset = offset
         break
      end
      local cmd_code = tvb(offset, 4):uint()
      local cmd_name = gfm_command_names[cmd_code]
      if cmd_name == nil then
         cmd_name = "unknown command"
      end
      if pinfo_cmd_name == "unknown command" or
         pinfo_cmd_name == "GFM_PROTO_COMPOUND_BEGIN" or
         pinfo_cmd_name == "GFM_PROTO_PUT_FD" then
         pinfo_cmd_name = cmd_name
         pinfo.cols.info = "Gfarm gfm request (" .. pinfo_cmd_name .. ")"
      end
      gfm_command_history[history_key][i] = cmd_code

      local subitem = item:add(gfm_proto, tvb(offset),
                               "Gfarm gfm protocol (request)")
      subitem:set_text(string.format("gfm command: %s", cmd_name))
      parse_xdr(tvb, subitem, "i", offset, "command", gfm_command_names)

      local parser = gfm_command_parsers[cmd_code]
      if parser == nil then
         break
      end
      local next_offset = parser.request(tvb, pinfo, subitem, offset)
      if next_offset == nil then
         break
      elseif next_offset < 0 then
         pinfo.desegment_len = DESEGMENT_ONE_MORE_SEGMENT
         pinfo.desegment_offset = offset
         break
      end
      subitem:set_len(next_offset - offset)

      offset = next_offset
      i = i + 1
   end
end

--
-- Response dissector.
--
function gfm_response_dissector(tvb, pinfo, root, history_key)
   local pinfo_cmd_name = "unknown command"
   pinfo.cols.protocol = gfm_proto.name
   pinfo.cols.info = "Gfarm gfm response (" .. pinfo_cmd_name .. ")"
   pinfo.desegment_len = 0
   local item = root:add(gfm_proto, tvb(), "Gfarm gfm protocol (response)")
   if gfm_command_history[history_key] == nil then
      pinfo.cols.info = "Gfarm gfm response (no corresponding request)"
      return
   end

   local i = 1
   local offset = pinfo.desegment_offset
   pinfo.desegment_offset = 0

   while offset < tvb():len() do
      if offset + 4 > tvb():len() then
         pinfo.desegment_len = DESEGMENT_ONE_MORE_SEGMENT
         pinfo.desegment_offset = offset
         break
      end
      local cmd_code = gfm_command_history[history_key][i]
      local cmd_name = gfm_command_names[cmd_code]
      if cmd_name == nil then
         cmd_name = "unknown command"
      end
      if pinfo_cmd_name == "unknown command" or
         pinfo_cmd_name == "GFM_PROTO_COMPOUND_BEGIN" or
         pinfo_cmd_name == "GFM_PROTO_PUT_FD" then
         pinfo_cmd_name = cmd_name
         pinfo.cols.info = "Gfarm gfm response (" .. pinfo_cmd_name .. ")"
      end

      local subitem = item:add(gfm_proto, tvb(offset),
                               "Gfarm gfm protocol (response)")
      subitem:set_text(string.format("gfm command: %s", cmd_name))

      local parser = gfm_command_parsers[cmd_code]
      if parser == nil then
         break
      end
      local next_offset = parser.response(tvb, pinfo, subitem, offset)
      if next_offset == nil then
         break
      elseif next_offset < 0 then
         pinfo.desegment_len = DESEGMENT_ONE_MORE_SEGMENT
         pinfo.desegment_offset = offset
         break
      end
      subitem:set_len(next_offset - offset)

      offset = next_offset
      i = i + 1
   end
end

--
-- Dissector.
--
function gfm_proto.dissector(tvb, pinfo, root)
   if pinfo.dst_port == gfm_port then
      gfm_last_request_number = pinfo.number
      local history_key = string.format("%s;%d;%s;%d;%d",
                                        tostring(pinfo.src),
                                        pinfo.src_port,
                                        tostring(pinfo.dst),
                                        pinfo.dst_port,
                                        pinfo.number)
      return gfm_request_dissector(tvb, pinfo, root, history_key)
   else
      request_number = gfm_response_to_request[pinfo.number]
      if request_number == nil then
         request_number = gfm_last_request_number
         gfm_response_to_request[pinfo.number] = request_number
      end
      local history_key = string.format("%s;%d;%s;%d;%d",
                                        tostring(pinfo.dst),
                                        pinfo.dst_port,
                                        tostring(pinfo.src),
                                        pinfo.src_port,
                                        request_number)
      return gfm_response_dissector(tvb, pinfo, root, history_key)
   end
end

--
-- Register the dissector.
--
local tcp_table = DissectorTable.get("tcp.port")
tcp_table:add(gfm_port, gfm_proto)
