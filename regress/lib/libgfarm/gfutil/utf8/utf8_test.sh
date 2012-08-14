#! /bin/sh

. ./regress.conf

LOGFILE=$testbin/utf8_test.log
rm -f $LOGFILE

test_utf8()
{
	$testbin/utf8_test "$2"
	RESULT=$?
	if [ "$RESULT" -ne $1 ]; then
		echo "utf8_test '$2' returns $RESULT (expected $1)" >> $LOGFILE
		exit $exit_fail
	fi

	echo "utf8_test '$2' returns $RESULT (OK)" >> $LOGFILE
}

#
#   Char. number range  |        UTF-8 octet sequence
#      (hexadecimal)    |              (binary)
#   --------------------+---------------------------------------------
#   0000 0000-0000 007F | 0xxxxxxx
#   0000 0080-0000 07FF | 110xxxxx 10xxxxxx
#   0000 0800-0000 FFFF | 1110xxxx 10xxxxxx 10xxxxxx
#   0001 0000-0010 FFFF | 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
#

#    
# Empty string (ok).
#
test_utf8 0 '' 

#
# The fist byte is "0xxxxxxx".  This is U+007F (ok).
#
#     U+0001 is \x01
#          \x01 is "00000001" (= "0xxxxxxx").
#     U+007F is \x7f
#          \x7f is "01111111" (= "0xxxxxxx").
#
test_utf8 0 '\x01' 
test_utf8 0 '\x7f' 

#
# The fist byte is "10xxxxxx" (error).
#
#     "10000000" (= "10xxxxxx") is \x80.
#     "10111111" (= "10xxxxxx") is \xbf.
#
test_utf8 1 '\x80'
test_utf8 1 '\x80\xbf'
test_utf8 1 '\x80\xbf\xbf'
test_utf8 1 '\x80\xbf\xbf\xbf'
test_utf8 1 '\xbf'
test_utf8 1 '\xbf\xbf'
test_utf8 1 '\xbf\xbf\xbf'
test_utf8 1 '\xbf\xbf\xbf\xbf'

#
# The fist byte is "110xxxxx", but no second byte (error).
#
#     "11000000" (= "110xxxxx") is \xc0.
#     "11011111" (= "110xxxxx") is \xdf.
#
test_utf8 1 '\xc0'
test_utf8 1 '\xdf'

#
# The fist byte is "110xxxxx" but the second byte is not "10xxxxxx". (error)
#
#     "11000000" (= "110xxxxx") is \xc0.
#     "11011111" (= "110xxxxx") is \xdf.
#     "10000000" (= "10xxxxxx") is \x80.
#     "10111111" (= "10xxxxxx") is \xbf.
#
test_utf8 1 '\xc0\x7f'
test_utf8 1 '\xc0\xc0'
test_utf8 1 '\xdf\x7f'
test_utf8 1 '\xdf\xc0'

#
# The fist byte is "110xxxxx" and the second byte is "10xxxxxx",
# but not {U+0080 <= codepoint <= U+07FF}. (error)
#
#     U+0080 is \xc2\x80.
#     \xc2\x80 - 1 is \xc1\xbf.
#         \xc1 is "11000001" (= "110xxxxx").
#         \xbf is "11011111" (= "10xxxxxx").
#
test_utf8 1 '\xc0\xbf'

#
# The fist byte is "110xxxxx" and the second byte is "10xxxxxx" and
# {U+0080 <= codepoint <= U+07FF}.
#
#     U+0080 is \xc2\x80.
#         \xc2 is "11000010" (= "110xxxxx").
#         \x80 is "10000000" (= "10xxxxxx").
#     U+07FF is \xdf\xbf.
#         \xdf is "11011111" (= "110xxxxx").
#         \xbf is "10111111" (= "10xxxxxx").
#
test_utf8 0 '\xc2\x80'
test_utf8 0 '\xdf\xbf'

#
# The fist byte is "1110xxxx" but no second byte. (error)
#
#     "11100000" (= "1110xxxx") is \xe0.
#     "11101111" (= "1110xxxx") is \xef.
#
test_utf8 1 '\xe0'
test_utf8 1 '\xef'

#
# The fist byte is "1110xxxx" and the second byte is "10xxxxxx", but
# no third byte (error).
#
#     "11100000" (= "1110xxxx") is \xe0.
#     "11101111" (= "1110xxxx") is \xef.
#     "10000000" (= "10xxxxxx") is \x80.
#     "10111111" (= "10xxxxxx") is \xbf.
#
test_utf8 1 '\xe0\x80'
test_utf8 1 '\xe0\xbf'
test_utf8 1 '\xef\x80'
test_utf8 1 '\xef\xbf'

#
# The fist byte is "1110xxxx" but the second is not "10xxxxxx" (error).
#
#     "11100000" (= "1110xxxx") is \xe0.
#     "11101111" (= "1110xxxx") is \xef.
#     "10000000" (= "10xxxxxx") is \x80.
#     "10111111" (= "10xxxxxx") is \xbf.
#
test_utf8 1 '\xe0\x7f\x80'
test_utf8 1 '\xe0\x7f\xbf'
test_utf8 1 '\xe0\xc0\x80'
test_utf8 1 '\xe0\xc0\xbf'
test_utf8 1 '\xef\x7f\x80'
test_utf8 1 '\xef\x7f\xbf'
test_utf8 1 '\xef\xc0\x80'
test_utf8 1 '\xef\xc0\xbf'

#
# The fist byte is "1110xxxx" and the second byte is "10xxxxxx", but
# the third byte is not "10xxxxxx" (error).
#
#     "11100000" (= "1110xxxx") is \xe0.
#     "11101111" (= "1110xxxx") is \xef.
#     "10000000" (= "10xxxxxx") is \x80.
#     "10111111" (= "10xxxxxx") is \xbf.
#
test_utf8 1 '\xe0\x80\x7f'
test_utf8 1 '\xe0\x80\xc0'
test_utf8 1 '\xe0\xbf\x7f'
test_utf8 1 '\xe0\xbf\xc0'
test_utf8 1 '\xef\x80\x7f'
test_utf8 1 '\xef\x80\xc0'
test_utf8 1 '\xef\xbf\x7f'
test_utf8 1 '\xef\xbf\xc0'

#
# The fist byte is "110xxxxx" and the second byte is "10xxxxxx", but
# but not {U+0800 <= codepoint <= U+FFFF}. (error)
#
#     U+0800 is \xe0\xa0\x80
#     \xe0\xa0\x80 - 1 is \xe0\9f\xbf.
#         \xe0 is "11100000" (= "1110xxxx").
#         \x9f is "10011111" (= "10xxxxxx").
#         \xbf is "11011111" (= "10xxxxxx").
#
test_utf8 1 '\xe0\x9f\xbf'

#
# The fist byte is "110xxxxx" and the second byte is "10xxxxxx", and
# {U+0800 <= codepoint <= U+FFFF}. (ok)
#
#     U+0800 is \xe0\xa0\x80
#         \xe0 is "11100000" (= "1110xxxx").
#         \xa0 is "10100000" (= "10xxxxxx").
#         \x80 is "10000000" (= "10xxxxxx").
#     U+FFFF is \xef\xbf\xbf
#         \xe0 is "11100000" (= "1110xxxx").
#         \xbf is "10111111" (= "10xxxxxx").
#
test_utf8 0 '\xe0\xa0\x80'
test_utf8 0 '\xef\xbf\xbf'

#
# The fist byte is "11110xxx" but no second byte. (error)
#
#     "11110000" (= "11110xxx") is \xf0.
#     "11110111" (= "11110xxx") is \xf7.
#
test_utf8 1 '\xf0'
test_utf8 1 '\xf7'

#
# The fist byte is "11110xxx" and the second byte is "10xxxxxx", but
# no third byte (error).
#
#     "11110000" (= "1110xxxx") is \xf0.
#     "11110111" (= "1110xxxx") is \xf7.
#     "10000000" (= "10xxxxxx") is \x80.
#     "10111111" (= "10xxxxxx") is \xbf.
#
test_utf8 1 '\xf0\x80'
test_utf8 1 '\xf0\xbf'
test_utf8 1 '\xf7\x80'
test_utf8 1 '\xf7\xbf'

#
# The fist byte is "11110xxx" and the second and third bytes are
# "10xxxxxx", but no fouth byte (error).
#
#     "11110000" (= "1110xxxx") is \xf0.
#     "11110111" (= "1110xxxx") is \xf7.
#     "10000000" (= "10xxxxxx") is \x80.
#     "10111111" (= "10xxxxxx") is \xbf.
#
test_utf8 1 '\xf0\x80\x80'
test_utf8 1 '\xf0\xbf\xbf'
test_utf8 1 '\xf7\x80\x80'
test_utf8 1 '\xf7\xbf\xbf'

#
# The fist byte is "1110xxxx" but the second is not "10xxxxxx" (error).
#
#     "11110000" (= "1110xxxx") is \xf0.
#     "11110111" (= "1110xxxx") is \xf7.
#     "10000000" (= "10xxxxxx") is \x80.
#     "10111111" (= "10xxxxxx") is \xbf.
#
test_utf8 1 '\xf0\x7f\x80\x80'
test_utf8 1 '\xf0\x7f\xbf\xbf'
test_utf8 1 '\xf0\xc0\x80\x80'
test_utf8 1 '\xf0\xc0\xbf\xbf'
test_utf8 1 '\xf7\x7f\x80\x80'
test_utf8 1 '\xf7\x7f\xbf\xbf'
test_utf8 1 '\xf7\xc0\x80\x80'
test_utf8 1 '\xf7\xc0\xbf\xbf'

#
# The fist byte is "1110xxxx" and the second byte is "10xxxxxx", but
# the third byte is not "10xxxxxx" (error).
#
#     "11110000" (= "1110xxxx") is \xf0.
#     "11110111" (= "1110xxxx") is \xf7.
#     "10000000" (= "10xxxxxx") is \x80.
#     "10111111" (= "10xxxxxx") is \xbf.
#
test_utf8 1 '\xf0\x80\x7f\x80'
test_utf8 1 '\xf0\x80\xc0\xbf'
test_utf8 1 '\xf0\xbf\x7f\x80'
test_utf8 1 '\xf0\xbf\xc0\xbf'
test_utf8 1 '\xf7\x80\x7f\x80'
test_utf8 1 '\xf7\x80\xc0\xbf'
test_utf8 1 '\xf7\xbf\x7f\x80'
test_utf8 1 '\xf7\xbf\xc0\xbf'

#
# The fist byte is "1110xxxx" and the second byte is "10xxxxxx", but
# the third byte is not "10xxxxxx" (error).
#
#     "11110000" (= "1110xxxx") is \xf0.
#     "11110111" (= "1110xxxx") is \xf7.
#     "10000000" (= "10xxxxxx") is \x80.
#     "10111111" (= "10xxxxxx") is \xbf.
#
test_utf8 1 '\xf0\x80\x7f\x80'
test_utf8 1 '\xf0\x80\xc0\xbf'
test_utf8 1 '\xf0\xbf\x7f\x80'
test_utf8 1 '\xf0\xbf\xc0\xbf'
test_utf8 1 '\xf7\x80\x7f\x80'
test_utf8 1 '\xf7\x80\xc0\xbf'
test_utf8 1 '\xf7\xbf\x7f\x80'
test_utf8 1 '\xf7\xbf\xc0\xbf'

#
# The fist byte is "110xxxxx" and the second and third bytes are
# "10xxxxxx", but the fourth byte is not "10xxxxxx" (error).
#
#     "11110000" (= "1110xxxx") is \xf0.
#     "11110111" (= "1110xxxx") is \xf7.
#     "10000000" (= "10xxxxxx") is \x80.
#     "10111111" (= "10xxxxxx") is \xbf.
#
test_utf8 1 '\xf0\x80\x80\x7f'
test_utf8 1 '\xf0\x80\x80\xc0'
test_utf8 1 '\xf0\xbf\xbf\x7f'
test_utf8 1 '\xf0\xbf\xbf\xc0'
test_utf8 1 '\xf7\x80\x80\x7f'
test_utf8 1 '\xf7\x80\x80\xc0'
test_utf8 1 '\xf7\xbf\xbf\x7f'
test_utf8 1 '\xf7\xbf\xbf\xc0'

#
# The fist byte is "1110xxxx" and the second, third and fourth bytes are
# "10xxxxxx", but not {U+10000 <= codepoint <= U+10FFFF}. (error)
#
#     U+10000 is \xf0\x90\x80\x80
#     \xf0\x90\x80\x80 - 1 is \f0\x8f\xbf.
#         \xf0 is "11110000" (= "11110xxx").
#         \x8f is "10001111" (= "10xxxxxx").
#         \xbf is "10111111" (= "10xxxxxx").
#     U+10FFFF is \xf4\x8f\xbf\xbf
#     \xf4\x8f\xbf\xbf + 1 is \xf4\x90\x80\x80.
#         \xf4 is "11110100" (= "11110xxx").
#         \x90 is "10010000" (= "10xxxxxx").
#         \x80 is "10000000" (= "10xxxxxx").
#
test_utf8 1 '\xf0\x8f\xbf\xbf'
test_utf8 1 '\xf4\x90\x80\x80'

#
# The fist byte is "1110xxxx" and the second, third and fourth bytes are
# "10xxxxxx" and {U+10000 <= codepoint <= U+10FFFF}. (ok)
#
#     U+10000 is \xf0\x90\x80\x80
#         \xf0 is "11110000" (= "11110xxx").
#         \x90 is "10010000" (= "10xxxxxx").
#         \x80 is "10000000" (= "10xxxxxx").
#     U+10FFFF is \xf4\x8f\xbf\xbf
#         \xf4 is "11110100" (= "11110xxx").
#         \x8f is "10001111" (= "10xxxxxx").
#         \xbf is "10111111" (= "10xxxxxx").
#
test_utf8 0 '\xf0\x90\x80\x80'
test_utf8 0 '\xf4\x8f\xbf\xbf'

#
# The fist byte is "111110xx" (error).
#
#     Note: "11111000" (= "111110xx") is \xf8.
#           "11111011" (= "111110xx") is \xfb.
#
test_utf8 1 '\xf8'
test_utf8 1 '\xf8\xbf'
test_utf8 1 '\xf8\xbf\xbf'
test_utf8 1 '\xf8\xbf\xbf\xbf'
test_utf8 1 '\xf8\xbf\xbf\xbf\xbf'
test_utf8 1 '\xfb'
test_utf8 1 '\xfb\xbf'
test_utf8 1 '\xfb\xbf\xbf'
test_utf8 1 '\xfb\xbf\xbf\xbf'
test_utf8 1 '\xfb\xbf\xbf\xbf\xbf'

#
# The fist byte is "1111110x" (error).
#
#     "11111100" (= "1111110x") is \xfc.
#     "11111101" (= "1111110x") is \xfd.
#
test_utf8 1 '\xfc'
test_utf8 1 '\xfc\xbf'
test_utf8 1 '\xfc\xbf\xbf'
test_utf8 1 '\xfc\xbf\xbf\xbf'
test_utf8 1 '\xfc\xbf\xbf\xbf\xbf'
test_utf8 1 '\xfc\xbf\xbf\xbf\xbf\xbf'
test_utf8 1 '\xfd'
test_utf8 1 '\xfd\xbf'
test_utf8 1 '\xfd\xbf\xbf'
test_utf8 1 '\xfd\xbf\xbf\xbf'
test_utf8 1 '\xfd\xbf\xbf\xbf\xbf'
test_utf8 1 '\xfd\xbf\xbf\xbf\xbf\xbf'

#
# The fist byte is "11111110" (error).
#
#     "11111110" is \xfe.
#
test_utf8 1 '\xfe'
test_utf8 1 '\xfe\xbf'
test_utf8 1 '\xfe\xbf\xbf'
test_utf8 1 '\xfe\xbf\xbf\xbf'
test_utf8 1 '\xfe\xbf\xbf\xbf\xbf'
test_utf8 1 '\xfe\xbf\xbf\xbf\xbf\xbf'
test_utf8 1 '\xfe\xbf\xbf\xbf\xbf\xbf\xbf'

#
# The fist byte is "11111111" (error).
#
#     "11111111" is \xff.
#
test_utf8 1 '\xff'
test_utf8 1 '\xff\xbf'
test_utf8 1 '\xff\xbf\xbf'
test_utf8 1 '\xff\xbf\xbf\xbf'
test_utf8 1 '\xff\xbf\xbf\xbf\xbf'
test_utf8 1 '\xff\xbf\xbf\xbf\xbf\xbf'
test_utf8 1 '\xff\xbf\xbf\xbf\xbf\xbf\xbf'
test_utf8 1 '\xff\xbf\xbf\xbf\xbf\xbf\xbf\xbf'

#
# A variety of characters.
#     U+0001 is \x01.
#     U+0080 is \xc2\x80..
#     U+0800 is \xe0\xa0\x80.
#     U+10000 is \xf0\x90\x80\x80.
#
test_utf8 0 '\x01\xc2\x80\xe0\xa0\x80\xf0\x90\x80\x80\x01'

#
# Surrogates.
#
#     U+D7FF is \xed\x9f\xbf.
#     U+D800 is \xed\xa0\x80.
#     U+DFFF is \xed\xbf\xbf.
#     U+E000 is \xee\x80\x80.
#
test_utf8 0 '\xed\x9f\xbf'
test_utf8 1 '\xed\xa0\xa0'
test_utf8 1 '\xed\xbf\xbf'
test_utf8 0 '\xee\x80\x80'

exit $exit_pass
