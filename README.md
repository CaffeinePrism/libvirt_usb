# libvirt_usb

I thought it was silly having to create xml files to passthrough USB devices into a VM with libvirt and passthrough with Spice doesn't always work.


This code was written partially as proof-of-concept. It works as intended, but has not been thoroughly tested.
Use at your own risk.


# TODO
- Proper exception flow and connection cleanup
- Remove stale entries from live domain
- Probably more
