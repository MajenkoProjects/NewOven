Majenko's Reflow Oven
=====================

This is the firmware for a reflow oven
based on the Picadillo-35T.

Parts used
----------

* Picadillo-35T
* Andrew James toaster oven
* 3x Fotek SSR (25A, 350V)
* Switched IEC C13 10A socket
* Chassis mount fuse holder and fuse
* 16 button keypad (0-9A-D*#)
* Gold heat shielding tape
* Thermocouple probe (K)
* MAX13855-K breakout board
* Meanwell 5V DIN mount PSU DR-15-5
* Short length of DIN rail
* Plastic sheeting (ABS or polystyrene)
* 4x 10K resistors
* 0.1" pitch headers
* M3 nuts and bolts
* Blood, sweat and tears

How it's done
-------------

This is written from memory. I hope I
have it all correct.

All the electrics are removed from the
side compartment of the oven. The heating
elements and fan are all that are left.
The SSRs are bolted to the floor of the
side compartment, mains facing in towards
the main oven.  The front panel is
removed. The keypad and Picadillo-35T
are mounted in the space where the front
panel was using the sheet plastic for
both filling of spacing and mounting
brackets.  The power socket and fuse
are mounted in the back of the oven.
The DIN rail is screwed to the back
of the side compartment with the power
supply mounted on it. A hole is cut in
the side of the side compartment
directly next to the USB connector
of the picadillo. This allows a USB plug
to be plugged in for programming.

Connect the earth of the power socket to
the casing of the oven.

Thermocouple enters the main oven from 
the rear and is routed round the back and
in to the side cavity.

Neutral is connected from power socket
to both heating elements and fan as well
as the PSU.

Live is connected from power socket to
fuse holder. Fused live then goes to 
screw 2 of the first SSR. From there the
fused live goes to screw 2 of the other
two SSRs and to the live of the PSU.

Screw 1 of the first SSR goes to the fan.
Screw 1 of the second and third SSRs go
to the two heating elements.

Power supply output is plugged direct
in to the 5V input of the Picadillo-35T

Keypad connects to pins 26-33 of the 
Picadillo. Pins 30-33 also connect to
pins 4-7 via 10K resistors.

SPI of the thermocouple connects to the 6
pin header of the Picadillo. CS connects
to pin 34.

Pins 35-37 connect to the SSR DC- pins. 
The SSR DC+ pins connect to the 5V pin 
of the Picadillo.

Program the board.

Enter the settings. Select the different
parameters and use the keypad to type
in the values you want. * is delete 
and # is enter.

Press reflow and hope.