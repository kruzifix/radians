# Radians
RLS is a randoming looping sequenzer which spits out melodies.
It is a module in the eurorack format and can be part of a modular synthesizer setup.
RLS outputs random voltages, triggered by either an internal clock or external trigger signal.
The probability to introduce new random values is controllable by a CV input and potentiometer.
Fully clockwise the sequence loop does not change.
By gradually turning the probabilty knob, more and more values of the sequence are randomly changed.
The length of the sequence can be controlled by a rotary switch.
Additionally the module features a Quantizer, quantizing random voltages into musical intervals.
With a push button the scale used by the quantizer is selectable.
The Quantizer is internally connected to the random output of the sequencer,
but can also be used with external signals.


by David Cukrowicz

## Specs

Device: Atmega168PA
Power Consumption: ??
Width: 10HP ?

## TODO
- [x] add schematics and other documentation
- [ ] finish firmware
- [ ] write manual