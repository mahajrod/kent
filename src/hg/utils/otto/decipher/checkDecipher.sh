#!/bin/sh -e
set -eEu -o pipefail
#	Do not modify this script, modify the source tree copy:
#	src/utils/decipher/checkDecipher.sh
#	This script is used via a cron job and kept in $HOME/bin/scripts/

#	cron jobs need to ensure this is true
#       current login requires the user be braney
umask 002

trap reportErr ERR

function reportErr
{
    echo "ERROR: DECIPHER pipeline failed"
    exit 1;
}


WORKDIR="/hive/data/outside/otto/decipher"
export WORKDIR

#	this is where we are going to work
if [ ! -d "${WORKDIR}" ]; then
    echo "ERROR in DECIPHER release watch, Can not find the directory:
    ${WORKDIR}" 
    exit 255
fi

cd "${WORKDIR}"

#ftppass=`cat ftp.pwd`
#gpgpass=`cat gpg.pwd`

perl login.perl > /dev/null 2>&1

if test decipher-variants*.gpg -nt lastUpdate
then
    today=`date +%F`
    mkdir -p $today

    CNV=decipher-cnvs*.gpg
    SNV=decipher-snvs*.gpg
    cp -p $CNV $SNV $today

    cd $today

    # unpack the gpg encrypted file
    gpg --batch --passphrase-file "../gpg.pwd.new" ${CNV}
    gpg --batch --passphrase-file "../gpg.pwd.new" ${SNV}

    # build the new DECIPHER track tables
    ../buildDecipher `basename $CNV .gpg` `basename $SNV .gpg`
    ../validateDecipher.sh hg19

    # now install
    for i in `cat ../decipher.tables`
    do 
	n=$i"New"
	o=$i"Old"
	hgsqlSwapTables hg19 $n $i $o -dropTable3
    done

    # now archive
    if [ ! -d ${WORKDIR}/archive ]; then
        mkdir -p ${WORKDIR}/archive
    fi
    cd ${WORKDIR}/archive
    mkdir -p hg19/${today}
    cd hg19/${today}
    printf "This directory contains a backup of the Decipher track data tables built on %s\n" "${today}" > README 
    for i in `cat ${WORKDIR}/decipher.tables`
    do
        hgsql --raw -Ne "show create table ${i}" hg19 > ${i}.sql
        hgsql -Ne "select * from ${i}" hg19 | gzip >  ${i}.txt.gz
    done

    echo "DECIPHER Installed `date`" 

    # Variants BED will be updated for both CNV and SNV updates
    cd ${WORKDIR}
    cp -p decipher-var*.gpg lastUpdate
else
    echo "No update"
fi

rm decipher-*
