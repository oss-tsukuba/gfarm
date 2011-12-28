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
-- Gfarm v2 gfs protocol dissector for Wireshark.
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
--          dofile("/somewhere/else/gfarm-gfs.lua")
-- 
--    where "/somewhere/..." is an actual path of this file.
--
-- Restrictions:
--   1. Packet dissection of some gfs commands have not been implemented yet.
--   2. Fragmentated packets are not dissected correctly.
--
require("bit")

--
-- Mapping table: command code -> name.
--
local gfs_command_names = {
   [  0] = 'GFS_PROTO_PROCESS_SET',
   [  1] = 'GFS_PROTO_OPEN_LOCAL',
   [  2] = 'GFS_PROTO_OPEN',
   [  3] = 'GFS_PROTO_CLOSE',
   [  4] = 'GFS_PROTO_PREAD',
   [  5] = 'GFS_PROTO_PWRITE',
   [  6] = 'GFS_PROTO_FTRUNCATE',
   [  7] = 'GFS_PROTO_FSYNC',
   [  8] = 'GFS_PROTO_FSTAT',
   [  9] = 'GFS_PROTO_CKSUM_SET',
   [ 10] = 'GFS_PROTO_LOCK',
   [ 11] = 'GFS_PROTO_TRYLOCK',
   [ 12] = 'GFS_PROTO_UNLOCK',
   [ 13] = 'GFS_PROTO_LOCK_INFO',
   [ 14] = 'GFS_PROTO_REPLICA_ADD',
   [ 15] = 'GFS_PROTO_REPLICA_ADD_FROM',
   [ 16] = 'GFS_PROTO_REPLICA_RECV',
   [ 17] = 'GFS_PROTO_STATFS',
   [ 18] = 'GFS_PROTO_COMMAND',
   [ 19] = 'GFS_PROTO_FHSTAT',
   [ 20] = 'GFS_PROTO_FHREMOVE',
   [ 21] = 'GFS_PROTO_STATUS',
   [ 22] = 'GFS_PROTO_REPLICATION_REQUEST',
   [ 23] = 'GFS_PROTO_REPLICATION_CANCEL',
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
local gfs_port = 600
local gfs_proto = Proto("gfarm-gfs", "gfarm gfs protocol")

--
-- Protofields.
--
local uint32_field = ProtoField.string("gfs.parameter.uint32",
                                       "uint32 parameter",
                                       base.DEC)
local uint64_field = ProtoField.string("gfs.parameter.uint32",
                                       "uint32 parameter",
                                       base.DEC)
local string_field = ProtoField.string("gfs.parameter.string",
                                       "string parameter")
local bytearray_field = ProtoField.bytes("gfs.parameter.bytes",
                                         "bytestream parameter")

gfs_proto.fields = {
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
-- Parse GFS_PROTO_PROCESS_SET.
--
function parse_gfs_process_set_request(tvb, pinfo, item, offset)
   -- IN i:type, b:shared_key, l:pid
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "type")
   offset = parse_xdr(tvb, item, "b", offset, "shared_key")
   offset = parse_xdr(tvb, item, "l", offset, "pid")
   return offset
end

function parse_gfs_process_set_response(tvb, pinfo, item, offset)
   -- OUT i:error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFS_PROTO_OPEN_LOCAL.
--
function parse_gfs_open_local_request(tvb, pinfo, item, offset)
   -- IN i:fd
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "fd")
   return offset
end

function parse_gfs_open_local_response(tvb, pinfo, item, offset)
   -- OUT i:error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFS_PROTO_OPEN.
--
function parse_gfs_open_request(tvb, pinfo, item, offset)
   -- IN 
end

function parse_gfs_open_response(tvb, pinfo, item, offset)
   -- OUT 
end

--
-- Parse GFS_PROTO_CLOSE.
--
function parse_gfs_close_request(tvb, pinfo, item, offset)
   -- IN i:fd
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "fd")
   return offset
end

function parse_gfs_close_response(tvb, pinfo, item, offset)
   -- OUT i:error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFS_PROTO_PREAD.
--
function parse_gfs_pread_request(tvb, pinfo, item, offset)
   -- IN i:fd, i:size, l:offset
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "fd")
   offset = parse_xdr(tvb, item, "i", offset, "size")
   offset = parse_xdr(tvb, item, "l", offset, "offset")
   return offset
end

function parse_gfs_pread_response(tvb, pinfo, item, offset)
   -- OUT i:error_code, b:data
   offset = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   offset = parse_xdr(tvb, item, "b", offset, "data")
   return offset
end

--
-- Parse GFS_PROTO_PWRITE.
--
function parse_gfs_pwrite_request(tvb, pinfo, item, offset)
   -- IN i:fd, b:buffer, l:offset
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "fd")
   offset = parse_xdr(tvb, item, "b", offset, "buffer")
   offset = parse_xdr(tvb, item, "l", offset, "offset")
   return offset
end

function parse_gfs_pwrite_response(tvb, pinfo, item, offset)
   -- OUT i:error_code, i:length
   offset = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   offset = parse_xdr(tvb, item, "i", offset, "length")
   return offset
end

--
-- Parse GFS_PROTO_FTRUNCATE.
--
function parse_gfs_ftruncate_request(tvb, pinfo, item, offset)
   -- IN i:fd, l:length
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "fd")
   offset = parse_xdr(tvb, item, "l", offset, "length")
   return offset
end

function parse_gfs_ftruncate_response(tvb, pinfo, item, offset)
   -- OUT i:error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFS_PROTO_FSYNC.
--
function parse_gfs_fsync_request(tvb, pinfo, item, offset)
   -- IN i:fd, i:operation
end

function parse_gfs_fsync_response(tvb, pinfo, item, offset)
   -- OUT i:error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFS_PROTO_FSTAT.
--
function parse_gfs_fstat_request(tvb, pinfo, item, offset)
   -- IN i:fd
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "fd")
   return offset
end

function parse_gfs_fstat_response(tvb, pinfo, item, offset)
   -- OUT i:error_code,
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      offset = parse_xdr(tvb, item, "l", offset, "size")
      offset = parse_xdr(tvb, item, "l", offset, "atime_sec", format_datetime)
      offset = parse_xdr(tvb, item, "i", offset, "atime_nsec")
      offset = parse_xdr(tvb, item, "l", offset, "mtime_sec", format_datetime)
      offset = parse_xdr(tvb, item, "i", offset, "mtime_nsec")
   end
   return offset
end

--
-- Parse GFS_PROTO_CKSUM_SET.
--
function parse_gfs_cksum_set_request(tvb, pinfo, item, offset)
   -- IN i:fd, s:cksum_type, b:cksum
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "fd")
   offset = parse_xdr(tvb, item, "s", offset, "cksum_type")
   offset = parse_xdr(tvb, item, "b", offset, "cksum")
   return offset
end

function parse_gfs_cksum_set_response(tvb, pinfo, item, offset)
   -- OUT i:error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFS_PROTO_LOCK.
--
function parse_gfs_lock_request(tvb, pinfo, item, offset)
   -- IN i:fd, l:start, l:len, i:type, i:whence
   offset = offset + 4
   offset = parse_xdr(tvb, item, "i", offset, "fd")
   offset = parse_xdr(tvb, item, "l", offset, "start")
   offset = parse_xdr(tvb, item, "l", offset, "len")
   offset = parse_xdr(tvb, item, "i", offset, "type")
   offset = parse_xdr(tvb, item, "i", offset, "whence", whence_names)
   return offset
end

function parse_gfs_lock_response(tvb, pinfo, item, offset)
   -- OUT i:error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFS_PROTO_TRYLOCK.
--
function parse_gfs_trylock_request(tvb, pinfo, item, offset)
   -- IN i:fd, l:start, l:len, i:type, i:whence
   return parse_gfs_lock_request(tvb, pinfo, item, offset)
end

function parse_gfs_trylock_response(tvb, pinfo, item, offset)
   -- OUT i:error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFS_PROTO_UNLOCK.
--
function parse_gfs_unlock_request(tvb, pinfo, item, offset)
   -- IN i:fd, l:start, l:len, i:type, i:whence
   return parse_gfs_lock_request(tvb, pinfo, item, offset)
end

function parse_gfs_unlock_response(tvb, pinfo, item, offset)
   -- OUT i:error_code
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

--
-- Parse GFS_PROTO_LOCK_INFO.
--
function parse_gfs_lock_info_request(tvb, pinfo, item, offset)
   -- IN i:fd, l:start, l:len, i:type, i:whence
   return parse_response_with_error_code_only(tvb, pinfo, item, offset)
end

function parse_gfs_lock_info_response(tvb, pinfo, item, offset)
   -- OUT i:error_code,
   local err
   offset, err = parse_xdr(tvb, item, "i", offset, "error_code", error_names)
   if err == 0 then
      offset = parse_xdr(tvb, item, "l", offset, "start")
      offset = parse_xdr(tvb, item, "l", offset, "len")
      offset = parse_xdr(tvb, item, "i", offset, "type")
      offset = parse_xdr(tvb, item, "s", offset, "host")
      offset = parse_xdr(tvb, item, "l", offset, "pid")
   end
   return offset
end

--
-- Parse GFS_PROTO_REPLICA_ADD.
--
function parse_gfs_replica_add_request(tvb, pinfo, item, offset)
   -- IN 
end

function parse_gfs_replica_add_response(tvb, pinfo, item, offset)
   -- OUT 
end

--
-- Parse GFS_PROTO_REPLICA_ADD_FROM.
--
function parse_gfs_replica_add_from_request(tvb, pinfo, item, offset)
   -- IN 
end

function parse_gfs_replica_add_from_response(tvb, pinfo, item, offset)
   -- OUT 
end

--
-- Parse GFS_PROTO_REPLICA_RECV.
--
function parse_gfs_replica_recv_request(tvb, pinfo, item, offset)
   -- IN 
end

function parse_gfs_replica_recv_response(tvb, pinfo, item, offset)
   -- OUT 
end

--
-- Parse GFS_PROTO_STATFS.
--
function parse_gfs_statfs_request(tvb, pinfo, item, offset)
   -- IN 
end

function parse_gfs_statfs_response(tvb, pinfo, item, offset)
   -- OUT 
end

--
-- Parse GFS_PROTO_COMMAND.
--
function parse_gfs_command_request(tvb, pinfo, item, offset)
   -- IN 
end

function parse_gfs_command_response(tvb, pinfo, item, offset)
   -- OUT 
end

--
-- Parse GFS_PROTO_FHSTAT.
--
function parse_gfs_fhstat_request(tvb, pinfo, item, offset)
   -- IN 
end

function parse_gfs_fhstat_response(tvb, pinfo, item, offset)
   -- OUT 
end

--
-- Parse GFS_PROTO_FHREMOVE.
--
function parse_gfs_fhremove_request(tvb, pinfo, item, offset)
   -- IN 
end

function parse_gfs_fhremove_response(tvb, pinfo, item, offset)
   -- OUT 
end

--
-- Parse GFS_PROTO_STATUS.
--
function parse_gfs_status_request(tvb, pinfo, item, offset)
   -- IN 
end

function parse_gfs_status_response(tvb, pinfo, item, offset)
   -- OUT 
end

--
-- Parse GFS_PROTO_REPLICATION_REQUEST.
--
function parse_gfs_replication_request_request(tvb, pinfo, item, offset)
   -- IN 
end

function parse_gfs_replication_request_response(tvb, pinfo, item, offset)
   -- OUT 
end

--
-- Parse GFS_PROTO_REPLICATION_CANCEL.
--
function parse_gfs_replication_cancel_request(tvb, pinfo, item, offset)
   -- IN 
end

function parse_gfs_replication_cancel_response(tvb, pinfo, item, offset)
   -- OUT 
end

--
-- Parse function for each command.
--
local gfs_command_parsers = {
   [  0] = {request  = parse_gfs_process_set_request,
	    response = parse_gfs_process_set_response},
   [  1] = {request  = parse_gfs_open_local_request,
	    response = parse_gfs_open_local_response},
   [  2] = {request  = parse_gfs_open_request,
	    response = parse_gfs_open_response},
   [  3] = {request  = parse_gfs_close_request,
	    response = parse_gfs_close_response},
   [  4] = {request  = parse_gfs_pread_request,
	    response = parse_gfs_pread_response},
   [  5] = {request  = parse_gfs_pwrite_request,
	    nresponse = parse_gfs_pwrite_response},
   [  6] = {request  = parse_gfs_ftruncate_request,
	    response = parse_gfs_ftruncate_response},
   [  7] = {request  = parse_gfs_fsync_request,
	    response = parse_gfs_fsync_response},
   [  8] = {request  = parse_gfs_fstat_request,
	    response = parse_gfs_fstat_response},
   [  9] = {request  = parse_gfs_cksum_set_request,
	    response = parse_gfs_cksum_set_response},
   [ 10] = {request  = parse_gfs_lock_request,
	    response = parse_gfs_lock_response},
   [ 11] = {request  = parse_gfs_trylock_request,
	    response = parse_gfs_trylock_response},
   [ 12] = {request  = parse_gfs_unlock_request,
	    response = parse_gfs_unlock_response},
   [ 13] = {request  = parse_gfs_lock_info_request,
	    response = parse_gfs_lock_info_response},
   [ 14] = {request  = parse_gfs_replica_add_request,
	    response = parse_gfs_replica_add_response},
   [ 15] = {request  = parse_gfs_replica_add_from_request,
	    response = parse_gfs_replica_add_from_response},
   [ 16] = {request  = parse_gfs_replica_recv_request,
	    response = parse_gfs_replica_recv_response},
   [ 17] = {request  = parse_gfs_statfs_request,
	    response = parse_gfs_statfs_response},
   [ 18] = {request  = parse_gfs_command_request,
	    response = parse_gfs_command_response},
   [ 19] = {request  = parse_gfs_fhstat_request,
	    response = parse_gfs_fhstat_response},
   [ 20] = {request  = parse_gfs_fhremove_request,
	    response = parse_gfs_fhremove_response},
   [ 21] = {request  = parse_gfs_status_request,
	    response = parse_gfs_status_response},
   [ 22] = {request  = parse_gfs_replication_request_request,
	    response = parse_gfs_replication_request_response},
   [ 23] = {request  = parse_gfs_replication_cancel_request,
	    response = parse_gfs_replication_cancel_response},
}

--
-- Packet number of a request packet that the dissector has previously
-- parsed.
--
local gfs_last_request_number = -1

--
-- Array to search a request packet number which corresponds
-- with a response packet number.
--
local gfs_response_to_request = {}

--
-- History of issued commands. 
--
local gfs_command_history = {}

--
-- Request dissector.
--
function gfs_request_dissector(tvb, pinfo, root, history_key)
   local pinfo_cmd_name = "unknown command"
   pinfo.cols.protocol = gfs_proto.name
   pinfo.cols.info = "Gfarm gfs request (" .. pinfo_cmd_name .. ")"
   pinfo.desegment_len = 0
   local item = root:add(gfs_proto, tvb(), "Gfarm gfs protocol (request)")
   gfs_command_history[history_key] = {}

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
      local cmd_name = gfs_command_names[cmd_code]
      if cmd_name == nil then
         cmd_name = "unknown command"
      end
      if pinfo_cmd_name == "unknown command" or
         pinfo_cmd_name == "GFM_PROTO_COMPOUND_BEGIN" or
         pinfo_cmd_name == "GFM_PROTO_PUT_FD" then
         pinfo_cmd_name = cmd_name
         pinfo.cols.info = "Gfarm gfs request (" .. pinfo_cmd_name .. ")"
      end
      gfs_command_history[history_key][i] = cmd_code

      local subitem = item:add(gfs_proto, tvb(offset),
                               "Gfarm gfs protocol (request)")
      subitem:set_text(string.format("gfs command: %s", cmd_name))
      parse_xdr(tvb, subitem, "i", offset, "command", gfs_command_names)

      local parser = gfs_command_parsers[cmd_code]
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
function gfs_response_dissector(tvb, pinfo, root, history_key)
   local pinfo_cmd_name = "unknown command"
   pinfo.cols.protocol = gfs_proto.name
   pinfo.cols.info = "Gfarm gfs response (" .. pinfo_cmd_name .. ")"
   pinfo.desegment_len = 0
   local item = root:add(gfs_proto, tvb(), "Gfarm gfs protocol (response)")
   if gfs_command_history[history_key] == nil then
      pinfo.cols.info = "Gfarm gfs response (no corresponding request)"
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
      local cmd_code = gfs_command_history[history_key][i]
      local cmd_name = gfs_command_names[cmd_code]
      if cmd_name == nil then
         cmd_name = "unknown command"
      end
      if pinfo_cmd_name == "unknown command" or
         pinfo_cmd_name == "GFM_PROTO_COMPOUND_BEGIN" or
         pinfo_cmd_name == "GFM_PROTO_PUT_FD" then
         pinfo_cmd_name = cmd_name
         pinfo.cols.info = "Gfarm gfs response (" .. pinfo_cmd_name .. ")"
      end

      local subitem = item:add(gfs_proto, tvb(offset),
                               "Gfarm gfs protocol (response)")
      subitem:set_text(string.format("gfs command: %s", cmd_name))

      local parser = gfs_command_parsers[cmd_code]
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
function gfs_proto.dissector(tvb, pinfo, root)
   if pinfo.dst_port == gfs_port then
      gfs_last_request_number = pinfo.number
      local history_key = string.format("%s;%d;%s;%d;%d",
                                        tostring(pinfo.src),
                                        pinfo.src_port,
                                        tostring(pinfo.dst),
                                        pinfo.dst_port,
                                        pinfo.number)
      return gfs_request_dissector(tvb, pinfo, root, history_key)
   else
      request_number = gfs_response_to_request[pinfo.number]
      if request_number == nil then
         request_number = gfs_last_request_number
         gfs_response_to_request[pinfo.number] = request_number
      end
      local history_key = string.format("%s;%d;%s;%d;%d",
                                        tostring(pinfo.dst),
                                        pinfo.dst_port,
                                        tostring(pinfo.src),
                                        pinfo.src_port,
                                        request_number)
      return gfs_response_dissector(tvb, pinfo, root, history_key)
   end
end

--
-- Register the dissector.
--
local tcp_table = DissectorTable.get("tcp.port")
tcp_table:add(gfs_port, gfs_proto)
