Toilet Roll Sensor
==================

Internet collected toilet roll poll. Constructed of a 3D printed base and a acrylic pole which the toilet paper rolls are stacked.

The presense of toilet rolls are sensed with a IR reflectance sensor. Reflected IR signal == roll preset, no reflection == no roll.

Uses Photon with WiFi connection which is high power and 20mA drive for 3x IR LEDs, however the Photon can be put in low power mode (no WiFi)
and roll sensing only needs to be done occasionally.

Why:
* Why not ;-)
* Guest bathrooms you don't check all that often - get notification on low toilet paper before your guess runs out.
* Day to day automation, never forget to order toilet paper again.

Design issues:

V1: 
* Somewhat high power using photon and WiFi, most likely batteries will runout before toilet paper!
* Multiboard design clumsy to solder together but flexible from 1 to 4 roll sensors
* limited connections means all leds are driven at once (@20mA), so 3 roll system sees 60mA spike
* PCB mounted AA battery holders were just a bit too big to fit in the acrylic tube so batteries embedded in the 3D printed base.

V2 Options:
* Replace Photon with Bluz (or Zigbee + SmartThings combo).
* Single board to allow better signal routing and less soldering of connections.
* Possible to use PCB mounted coin cells if Bluz low power enough 



