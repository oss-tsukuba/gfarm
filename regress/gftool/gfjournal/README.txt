Regression tests for gfjournal

Each shell script (*.sh) performs a test case which uses the corresponding
journal file (*.gmj).  The expected outputs of 'gfjournal' and 'gfjournal -m'
commands with the journal file are stored as the corresponding '*.out' and
'*-m.out' files.

For more details about journal files of the tests, use 'gfjournaldump',
like this:

    gfjournaldump JOURNAL-FILE


Normal Test Cases
=================

no_record.sh
------------
The journal file of this test case has no record.  In other words, it
has a file header only.

one_transaction.sh
------------------
The journal file of this test case has only one transaction.  In the
journal file, the following records are written in that order.

    record: seqnum=2 (transaction begin)
    record: seqnum=3
    record: seqnum=4 (transaction end)

two_transactions1.sh
--------------------
The journal file of this test case has two transactions.

    record: seqnum=2 (transaction begin)
    record: seqnum=3
    record: seqnum=4 (transaction end)
    record: seqnum=5 (transaction begin)
    record: seqnum=6
    record: seqnum=7 (transaction end)

two_transactions2.sh
--------------------
Same as 'two_transactions1.sh', but the order of records differs.

    record: seqnum=5 (transaction begin)
    record: seqnum=6
    record: seqnum=7 (transaction end)
    record: seqnum=2 (transaction begin)
    record: seqnum=3
    record: seqnum=4 (transaction end)

two_transactions3.sh
--------------------
Same as 'two_transactions2.sh', but the order of records differs.

    record: seqnum=6
    record: seqnum=7 (transaction end)
    record: seqnum=2 (transaction begin)
    record: seqnum=3
    record: seqnum=4 (transaction end)
    record: seqnum=5 (transaction begin)

gap_end.sh
----------
gfmd uses a journal file like as a ring buffer.  It overwrites old records
successively.  Since length of each record is variable and a record is not
written separately at the end and the beginning of the file, a journal
file may have one or two gaps, at the middle and end of the file.

The journal file of this test case has a gap at the end of the file.

    record: seqnum=5 (transaction begin)
    record: seqnum=6
    record: seqnum=7 (transaction end)
    record: seqnum=2 (transaction begin)
    record: seqnum=3
    record: seqnum=4 (transaction end)
    <gap>

gap_middle.sh
-------------
This test case is similar to 'gap_end.sh', but it has a gap at the middle
of the journal file.

    record: seqnum=5 (transaction begin)
    record: seqnum=6
    record: seqnum=7 (transaction end)
    <gap>
    record: seqnum=2 (transaction begin)
    record: seqnum=3
    record: seqnum=4 (transaction end)

gap_middle_end.sh
-----------------
This test case is similar to 'gap_end.sh', but it has gaps at the middle
and the end of the journal file.

    record: seqnum=5 (transaction begin)
    record: seqnum=6
    record: seqnum=7 (transaction end)
    <gap>
    record: seqnum=2 (transaction begin)
    record: seqnum=3
    record: seqnum=4 (transaction end)
    <gap>


Irregular Test Cases
====================

bad_record_crc1.sh
------------------
The journal file contains a record with an incorrect CRC32 checksum.
The transaction of the record is discarded.

    record: seqnum=2 (transaction begin)
    record: seqnum=3
    record: seqnum=4 (transaction end)
    record: seqnum=5 (transaction begin)
    record: seqnum=6
    record: seqnum=7 (transaction end; bad checksum)

bad_record_crc2.sh
------------------
Same as 'bad_record_crc1.sh', but the order of records differs.

    record: seqnum=5 (transaction begin)
    record: seqnum=6
    record: seqnum=7 (transaction end; bad checksum)
    record: seqnum=2 (transaction begin)
    record: seqnum=3
    record: seqnum=4 (transaction end)


bad_record_magic1.sh
--------------------
The journal file contains a record with an incorrect magic ID.  The
transaction of the record is ignored.

    record: seqnum=2 (transaction begin)
    record: seqnum=3
    record: seqnum=4 (transaction end)
    record: seqnum=5 (transaction begin)
    record: seqnum=6
    record: seqnum=7 (transaction end; bad magic)

Note 'that gfjournaldump' doesn't recognize a record with an incorrect
magic ID as a record, but junk data.

bad_record_magic2.sh
--------------------
Same as 'bad_record_magic1.sh', but the order of records differs.

    record: seqnum=5 (transaction begin)
    record: seqnum=6
    record: seqnum=7 (transaction end; bad magic)
    record: seqnum=2 (transaction begin)
    record: seqnum=3
    record: seqnum=4 (transaction end)

lack_record_crc.sh
------------------
The last record of the journal file lacks a checksum field.

    record: seqnum=2 (transaction begin)
    record: seqnum=3
    record: seqnum=4 (transaction end)
    record: seqnum=5 (transaction begin)
    record: seqnum=6
    record: seqnum=7 (transaction end; lack checksum)

Note that 'gfjournaldump' doesn't recognize the last record as a record,
but junk data.

lack_record_data.sh
-------------------
Same as 'bad_record_magic1.sh', but it also lacks a data field.

    record: seqnum=2 (transaction begin)
    record: seqnum=3
    record: seqnum=4 (transaction end)
    record: seqnum=5 (transaction begin)
    record: seqnum=6
    record: seqnum=7 (transaction end; lack data)

lack_record_datalen.sh
----------------------
Same as 'bad_record_magic2.sh', but it also lacks a datalen field.

    record: seqnum=2 (transaction begin)
    record: seqnum=3
    record: seqnum=4 (transaction end)
    record: seqnum=5 (transaction begin)
    record: seqnum=6
    record: seqnum=7 (transaction end; lack datalen)
