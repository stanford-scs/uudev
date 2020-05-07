% uudev(1)
% David Mazières
%

# NAME

uudev - micro-udev service

# SYNOPSIS

uudev [-v] [-c conf] \
uudev [-v] -m \
uudev [-v] -p /dev/path

# DESCRIPTION

Micro-udev is a trivial udev-like service that runs commands in the
context of a (non-root) user session when devices are added or removed
from the system.  It fires off shell commands based on properties of
devices assigned by the kernel and system-level udev daemon.

There are three modes.  The main mode (with no options) listens for
device events and fires off rules based on your `uudev.conf`
configuration file.  With `-vm`, uudev runs in monitor mode where it
listens for events and prints what is happening, so you can find
appropriate properties on which to base rules.  With `-vp` uudev
prints the properties of a particular existing device node in the file
system.

Typically you will want to run `uudev -vp` or `uudev -vm` and based on
the output create a `$HOME/.config/uudev.conf` file.  Then test that
it does what you want by running `uudev -v`.  Finally, to enable
`uudev` by default run:

	systemctl --user enable --now uudev

This can be reversed by changing the `enable` to `disable`, or
manually removing the link in
`$HOME/.config/systemd/user/default.target.wants`.  Any time you edit
the configuration file, you must restart `uudev`.  Once you are using
systemd, you can do this with:

	systemctl --user restart uudev

## OPTIONS

`-v`
:	Increases the level of verbosity.

`-c`
:	Specifies a non-default configuration file location.

`-m`
:	Runs uudev in monitor mode.  This is most useful in conjunction
with `-v` to see all the device properties you can test for.

`-p`
:	Prints the devpath of a device in the file system.  Most useful in
conjunction with `-v`, to get all the device properties you can test
for.

# CONFIGURATION FILE FORMAT

The configuration file consists of a of series stanzas, each of which
starts with a predicate line (beginning `*`) followed by multiple
lines of shell script (not beginning `*`).  Every time there's a
device event matching one or more non-preamble predicates, the shell
scripts of stanzas with matching predicates are concatenated and piped
to `/bin/sh` (with environment variables set for the device event
properties).

The format of a test-line is:

> _TYPE_ {_PROPERTY_ _OP_ `"`_VALUE_`",`}*

_TYPE_ always tarts with `*` and is optionally followed by one or both
of `?` and `!`.  The `!` signifies that the shell code should also be
run whenever `uudev` starts up.  This flag is useful, for instance, if
you want to configure a device such a bluetooth keyboard when you
first start `uudev` (in case the device is already connected) as well
as whenever the device subsequently connects.  When starting up, `!`
rules are run without any device property environment variables, since
there is no device event.  You can test for this by the lack of an
`ACTION` environment variable.

The `?` flag signifies that the shell script is a "preamble" intended
to be prefixed to other rules.  Preamble shell code will only run if a
non-preamble test-line matches.  This flag is useful to set a shell
variable or define shell functions that apply to all subsequent rules,
or conceivably to filter out certain events by conditionally calling
exit before later stanzas can execute.  For example, if you want to
see all the commands being executed, you cal place this at the top of
your file:

    *!?
    set -x

Because the test-line is empty, it matches every device event.  But
because it is marked with a '?', it will not cause a shell to be
spawned for every device event, only ones that also match a
non-preamble line.  However, every time a shell does run it will begin
with `set -x`, which causes the shell to print the commands it is
executing.

The remainder of the test-line consists of tests separated by commas.
Each test checks a _PROPERTY_ against a _VALUE_.  _OP_ can be either
`==` or `!=` to check for equality or inequality.  Note that _VALUE_
must always be enclosed in double quotes, and no wildcard matching is
supported.  If _VALUE_ is empty, it also matches a missing property.
No character continuation lines or character escape sequences are
permitted, so you can't test against a string containing double
quotes.  A property either exactly matches the test value or does not.
You can always implement more refined tests in the shell code
following the predicate.

The `DEVLINK` property is special.  Since the system udev may create
multiple inks to a device, `DEVLINK=="`_VALUE_`"` matches if any of
the device's links is _VALUE_.

# EXAMPLES

To run `xmodmap` every time a keyboard is plugged in, you might use
this configuration:

	*! ACTION=="add", ID_INPUT_KEYBOARD=="1", MAJOR!=""
	xmodmap "$HOME/.mykeymap"

Note `MAJOR!=""` avoids matching devices that get added without any
device node.  This is because sometimes keyboard devices get added
twice, once as a real device and once with `TAGS=:power-switch:`.
Why?  Who knows--the point of uudev is that you don't need to
understand all this systemd stuff and can just create rules by trial
and error.

If you have a Wacom tablet mapped to its own display, the stylus area
will get uncalibrated each time the screen saver comes on.  You can
fix this with the following stanza (if your tablet is xinerama display
`HEAD-2`):

	*! ACTION=="bind", DRIVER=="wacom", HID_UNIQ!=""
	for dev in $(xsetwacom list | sed -ne 's/.*id: \([0-9]*\).*/\1/p'); do
	    xsetwacom set $dev MapToOutput HEAD-2
	done

Why match for empty `HID_UNIQ`?  Again, no idea--this is just trial
and error.  There seem to be multiple devices that get added for the
tablet, so to avoid running this command multiple times, or, worse,
running it too early when not all the devices are ready (e.g., the
touch screen might be ready but not the stylus) testing for an empty
or missing `HID_UNIQ` seems to do the trick.

# ENVIRONMENT

uudev just passes environment variables through to the shell code it
executes.  However, it also sets all device properties as environment
variables.

# FILES

The default configuration file location is `$HOME/.config/uudev.conf`.

# BUGS

There are no continuation lines.  There is no way to escape
characters, including no way to match against strings that contain
double quotes or newlines.  There's no way to write a line of shell
code that begins with a `*` character (as might be useful in a
here-document).  There aren't really any comments in the configuration
file (other than above the first predicate line); though of course you
can use shell comments, they get passed through to the shell.

# SEE ALSO

udev(7)