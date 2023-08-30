#
# Constant values related with journal file.
#
module GFJournalFileConsts
  #
  # Header structure.
  #
  HEADER_MAGIC           = "GfMj"

  HEADER_LENGTH          = 4096
  HEADER_MAGIC_LENGTH    = 4
  HEADER_VERSION_LENGTH  = 4
  HEADER_RESERVED_LENGTH = 4088

  HEADER_MAGIC_OFFSET    = 0
  HEADER_VERSION_OFFSET  = HEADER_MAGIC_OFFSET   + HEADER_MAGIC_LENGTH
  HEADER_RESERVED_OFFSET = HEADER_VERSION_LENGTH + HEADER_VERSION_OFFSET

  #
  # Record structure.
  #
  RECORD_MAGIC          = "GfMr"
  RECORD_MAGIC_LENGTH   = 4
  RECORD_SEQNUM_LENGTH  = 8
  RECORD_OPEID_LENGTH   = 4
  RECORD_DATALEN_LENGTH = 4
  RECORD_CRC32_LENGTH   = 4

  RECORD_MAGIC_OFFSET   = 0
  RECORD_SEQNUM_OFFSET  = RECORD_MAGIC_OFFSET   + RECORD_MAGIC_LENGTH
  RECORD_OPEID_OFFSET   = RECORD_SEQNUM_OFFSET  + RECORD_SEQNUM_LENGTH
  RECORD_DATALEN_OFFSET = RECORD_OPEID_OFFSET   + RECORD_OPEID_LENGTH
  RECORD_DATA_OFFSET    = RECORD_DATALEN_OFFSET + RECORD_DATALEN_LENGTH

  #
  # Operation IDs.
  #
  OPEID_BEGIN               =  1
  OPEID_END                 =  2

  OPEID_HOST_ADD            =  3
  OPEID_HOST_MODIFY         =  4
  OPEID_HOST_REMOVE         =  5

  OPEID_USER_ADD            =  6
  OPEID_USER_MODIFY         =  7
  OPEID_USER_REMOVE         =  8

  OPEID_GROUP_ADD           =  9
  OPEID_GROUP_MODIFY        = 10
  OPEID_GROUP_REMOVE        = 11

  OPEID_INODE_ADD           = 12
  OPEID_INODE_MODIFY        = 13
  OPEID_INODE_GEN_MODIFY    = 14
  OPEID_INODE_NLINK_MODIFY  = 15
  OPEID_INODE_SIZE_MODIFY   = 16
  OPEID_INODE_MODE_MODIFY   = 17
  OPEID_INODE_USER_MODIFY   = 18
  OPEID_INODE_GROUP_MODIFY  = 19
  OPEID_INODE_ATIME_MODIFY  = 20
  OPEID_INODE_MTIME_MODIFY  = 21
  OPEID_INODE_CTIME_MODIFY  = 22

  OPEID_INODE_CKSUM_ADD     = 23
  OPEID_INODE_CKSUM_MODIFY  = 24
  OPEID_INODE_CKSUM_REMOVE  = 25

  OPEID_FILECOPY_ADD        = 26
  OPEID_FILECOPY_REMOVE     = 27

  OPEID_DEADFILECOPY_ADD    = 28
  OPEID_DEADFILECOPY_REMOVE = 29

  OPEID_DIRENTRY_ADD        = 30
  OPEID_DIRENTRY_REMOVE     = 31

  OPEID_SYMLINK_ADD         = 32
  OPEID_SYMLINK_REMOVE      = 33

  OPEID_XATTR_ADD           = 34
  OPEID_XATTR_MODIFY        = 35
  OPEID_XATTR_REMOVE        = 36
  OPEID_XATTR_REMOVEALL     = 37

  OPEID_QUOTA_ADD           = 38
  OPEID_QUOTA_MODIFY        = 39
  OPEID_QUOTA_REMOVE        = 40

  OPEID_MDHOST_ADD          = 41
  OPEID_MDHOST_MODIFY       = 42
  OPEID_MDHOST_REMOVE       = 43

  OPEID_FSNGROUP_MODIFY     = 44

  OPEID_NOP                 = 45

  OPEID_QUOTA_DIRSET_ADD    = 46
  OPEID_QUOTA_DIRSET_MODIFY = 47
  OPEID_QUOTA_DIRSET_REMOVE = 48

  OPEID_QUOTA_DIR_ADD       = 49
  OPEID_QUOTA_DIR_REMOVE    = 50

  OPEID_USER_AUTH_ADD       = 51
  OPEID_USER_AUTH_MODIFY    = 52
  OPEID_USER_AUTH_REMOVE    = 53

  #
  # Titles of operation ID.
  #
  OPEID_TITLES = {
    OPEID_BEGIN               => "BEGIN",
    OPEID_END                 => "END",

    OPEID_HOST_ADD            => "HOST_ADD",
    OPEID_HOST_MODIFY         => "HOST_MODIFY",
    OPEID_HOST_REMOVE         => "HOST_REMOVE",

    OPEID_USER_ADD            => "USER_ADD",
    OPEID_USER_MODIFY         => "USER_MODIFY",
    OPEID_USER_REMOVE         => "USER_REMOVE",

    OPEID_GROUP_ADD           => "GROUP_ADD",
    OPEID_GROUP_MODIFY        => "GROUP_MODIFY",
    OPEID_GROUP_REMOVE        => "GROUP_REMOVE",

    OPEID_INODE_ADD           => "INODE_ADD",
    OPEID_INODE_MODIFY        => "INODE_MODIFY",
    OPEID_INODE_GEN_MODIFY    => "INODE_GEN_MODIFY",
    OPEID_INODE_NLINK_MODIFY  => "INODE_NLINK_MODIFY",
    OPEID_INODE_SIZE_MODIFY   => "INODE_SIZE_MODIFY",
    OPEID_INODE_MODE_MODIFY   => "INODE_MODE_MODIFY",
    OPEID_INODE_USER_MODIFY   => "INODE_USER_MODIFY",
    OPEID_INODE_GROUP_MODIFY  => "INODE_GROUP_MODIFY",
    OPEID_INODE_ATIME_MODIFY  => "INODE_ATIME_MODIFY",
    OPEID_INODE_MTIME_MODIFY  => "INODE_MTIME_MODIFY",
    OPEID_INODE_CTIME_MODIFY  => "INODE_CTIME_MODIFY",

    OPEID_INODE_CKSUM_ADD     => "INODE_CKSUM_ADD",
    OPEID_INODE_CKSUM_MODIFY  => "INODE_CKSUM_MODIFY",
    OPEID_INODE_CKSUM_REMOVE  => "INODE_CKSUM_REMOVE",

    OPEID_FILECOPY_ADD        => "FILECOPY_ADD",
    OPEID_FILECOPY_REMOVE     => "FILECOPY_REMOVE",

    OPEID_DEADFILECOPY_ADD    => "DEADFILECOPY_ADD",
    OPEID_DEADFILECOPY_REMOVE => "DEADFILECOPY_REMOVE",

    OPEID_DIRENTRY_ADD        => "DIRENTRY_ADD",
    OPEID_DIRENTRY_REMOVE     => "DIRENTRY_REMOVE",

    OPEID_SYMLINK_ADD         => "SYMLINK_ADD",
    OPEID_SYMLINK_REMOVE      => "SYMLINK_REMOVE",

    OPEID_XATTR_ADD           => "XATTR_ADD",
    OPEID_XATTR_MODIFY        => "XATTR_MODIFY",
    OPEID_XATTR_REMOVE        => "XATTR_REMOVE",
    OPEID_XATTR_REMOVEALL     => "XATTR_REMOVEALL",

    OPEID_QUOTA_ADD           => "QUOTA_ADD",
    OPEID_QUOTA_MODIFY        => "QUOTA_MODIFY",
    OPEID_QUOTA_REMOVE        => "QUOTA_REMOVE",

    OPEID_MDHOST_ADD          => "MDHOST_ADD",
    OPEID_MDHOST_MODIFY       => "MDHOST_MODIFY",
    OPEID_MDHOST_REMOVE       => "MDHOST_REMOVE",

    OPEID_FSNGROUP_MODIFY     => "FSNGROUP_MODIFY",

    OPEID_NOP                 => "NOP",

    OPEID_QUOTA_DIRSET_ADD    => "QUOTA_DIRSET_ADD",
    OPEID_QUOTA_DIRSET_MODIFY => "QUOTA_DIRSET_MODIFY",
    OPEID_QUOTA_DIRSET_REMOVE => "QUOTA_DIRSET_REMOVE",

    OPEID_QUOTA_DIR_ADD       => "QUOTA_DIR_ADD",
    OPEID_QUOTA_DIR_REMOVE    => "QUOTA_DIR_REMOVE",

    OPEID_USER_AUTH_ADD       => "USER_AUTH_ADD",
    OPEID_USER_AUTH_MODIFY    => "USER_AUTH_MODIFY",
    OPEID_USER_AUTH_REMOVE    => "USER_AUTH_REMOVE",
  }
end
