#!/bin/sh

if [ $# -ne 2 ]; then
	echo "Please enter a file directory followed by a search string as arguements"
	exit 1
fi

FILESDIR=$1
SEARCHSTR=$2

if [ ! -d "$FILESDIR" ]; then
	echo "$FILESDIR is not a directory"
	exit 1
fi

NUM_FILES=$(find "$FILESDIR" -type f | wc -l)

NUM_MATCHES=$(grep -r "$SEARCHSTR" "$FILESDIR" | wc -l)

echo "The number of files are $NUM_FILES and the number of matching lines are $NUM_MATCHES"
