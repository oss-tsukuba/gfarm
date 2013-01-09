#! /bin/sh
#
# $Id$

GFQ_DIR=/tmp/.gfq-$$
rm -rf $GFQ_DIR

. gfq_setup.sh $*
. gfq_commit.sh
