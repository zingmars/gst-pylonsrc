#!/bin/bash

SYSEHCI=/sys/bus/pci/drivers/xhci_hcd

if [[ $EUID != 0 ]] ; then
 echo "This must be run as root!"
 exit 1
fi

if ! cd $SYSEHCI ; then
 echo "Failed to change directory to $SYSEHCI."
 exit 1
fi

for i in ????:??:??.? ; do
 echo -n "$i" > unbind
 echo -n "$i" > bind
done
