Both the S3 and C3 boards interface with each other via ESP-NOW on channel 6.


S3 assumes the role of "master", C3 assumes the role of "servant"

S3 sets up the AP connection and C3 connects to it. 

The S3 takes input from the user, which corresponds to PWM freq changes inside of the C3. 

The C3 module is interfaced with a motor driver from pololu, the DRV8835

https://www.pololu.com/product/2135

The Benable and BPhase pins on this driver are connected to pins 2 and 3 on the C3 module, respectively. 

the PWM range is from 60KHz to 160KHz.