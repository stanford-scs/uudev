# Micro-user-udev service

`uudev` is a linux program than launches other programs in response to
devices being added and removed from the system.  Unlike the
system-level [udev] program, `uudev` is designed to be run by users in
their Xorg sessions.  Hence it can easily do things like change key
bindings when a keyboard is connected, or kill your screen saver when
your bluetooth phone connects.

The configuration syntax is much simpler than [udev].  You still match
events on properties, but actions consist of arbitrary multi-line
shell code.  It doesn't provide any device configuration utilities,
but does give you access to all the properties set by the various
system [udev] rules.

See the [man page](uudev.1.md) for more documentation and examples.

[udev]: http://www.reactivated.net/writing_udev_rules.html
