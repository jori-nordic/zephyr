.. _bluetooth-hci-pwr-ctrl-sample:

Bluetooth: HCI VS Scan Request
##############################

Overview
********

This simple application is a usage example to manage HCI VS commands to obtain
scan equest events even using legacy advertisements, yet saving ~ 5.5 kB RAM,
which is quite important when the broadcaster role is added to the central role
and RAM usage is critical.
This sample implements only the broadcaster role, than can use either extended
or legacy advertisements and the peripheral role with connection can also be
added, depending on configuration choices.

Requirements
************

* A board with BLE support
* A central device & monitor (e.g. nRF Connect) to check the advertiments and
  send scan requests.

Building and Running
********************

This sample can be found under :zephyr_file:`samples/bluetooth/hci_vs_scan_req`
in the Zephyr tree.

See :ref:`bluetooth samples section <bluetooth-samples>` for details.
