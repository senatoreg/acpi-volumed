#
# Regular cron jobs for the acpi-volumed package
#
0 4	* * *	root	[ -x /usr/bin/acpi-volumed_maintenance ] && /usr/bin/acpi-volumed_maintenance
