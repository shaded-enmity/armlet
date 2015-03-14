#!/bin/bash

#
# armLET 
# Pavel Odvody - 2014 
#

ARM_MANUAL_PATTERN='^DDI0487A_._armv8_arm([a-z1-9_]*)\.pdf$'
ARM_MANUAL_DIR=$PWD
ARM_MANUAL_FILE=

PDFTOTEXT_ARGUMENTS='-layout'
PDFTOTEXT='pdftotext'

# hardcoded page no's for the time being
declare -a PAGES=('230 383 aarch64_system' '391 774 aarch64_base' '780 1403 aarch64_simdfp'\
 '2534 3027 aarch32_a32t32' '3034 3073 aarch32_system' '3076 3395 aarch32_simdfp'\
 '4878 5058 armv8_pseudocode_library' '5059 5080 armv8_pseudocode_definitions') 

function process_section() {
	echo "     + dumping $3"
	$PDFTOTEXT $PDFTOTEXT_ARGUMENTS -f $1 -l $2 $ARM_MANUAL_DIR$ARM_MANUAL_FILE\
	       	$ARM_MANUAL_DIR$3

	# skip first 12 lines at the start of each section
	# and also get rid of trailing lines
	tail -n+12 $ARM_MANUAL_DIR$3 | grep -Ev 'ARM DDI 0487A\.a*|ID090413*' > "$ARM_MANUAL_DIR$3.txt"
}

function find_manual_file() {
	ARM_MANUAL_FILE=$(ls $ARM_MANUAL_DIR | grep -iP $ARM_MANUAL_PATTERN)
}

function pre_conditions() {
	[ -e "$ARM_MANUAL_DIR$ARM_MANUAL_FILE" ] || exit 1
	[ -e $(whereis -b $PDFTOTEXT | cut -d" " -f2) ] || exit 2
	return 0
}

find_manual_file
pre_conditions

[ "$?" == "0" ] || (echo "pdftotext or arm manual file missing or misplaced/misnamed. ($?)" ; exit 1)

echo "[+] Processing ${#PAGES[@]} sections ..."

for (( i = 0; i < ${#PAGES[@]}; i++ )); do
	process_section ${PAGES[$i]}
done
