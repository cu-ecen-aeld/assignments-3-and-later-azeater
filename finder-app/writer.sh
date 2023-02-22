#!/bin/bash

if [ $# -ne 2 ]; then
	echo "Please enter a write directory followed by a string as arguements"
	exit 1
fi

WRITEFILE=$1
WRITESTR=$2

mkdir -p "$(dirname "$WRITEFILE")"

echo "$WRITESTR" > "$WRITEFILE"

if [ ! -f "$WRITEFILE" ]; then
	echo "Failed to create file $WRITEFILE"
	exit 1
fi

