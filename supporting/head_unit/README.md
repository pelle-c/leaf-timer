# CAN-decoder for Android head unit

## Good sources

- https://github.com/dalathegreat/leaf_can_bus_messages
- https://github.com/darkspr1te/canbox
- https://github.com/runmousefly/Work
- https://xdaforums.com/t/canbox-can-decoder-reverse-engineering.4057759/page-2


## TODO

- Figure out why speed on home screen is not updated, while speed in vehicle info app is.


## Testing 

Android HU: Raise/VW/PQ/Low

### Some messages
```
"46, 40, 2, 2, 1, 0" -> switches to audio
"46, 41, 2, 2, 2, 0" - set volume ->2 ("last '2'")
"46, 65,13,2,1,0,0,0,0,0,0,0,0,0,0,0,0"  -> 0 grader
[46, 65, 13, 2, 1, 0, 0, 0, 0, 0, 0, 20, 0, 0, 0, 0, 154] -> +2 degrees
"46, 65,13,2,0,0,0,0,0,0,255,20,0,0,0,0,0" -> -23.6 grader
[46, 65, 13, 2, 20, 1, 10, 1, 200, 1, 255, 20, 1, 1, 1, 1, chk] -> 5121RPM, 512.1V,25.61km/h,65793km,-23.6degrees
2E 41 0D 02 00 00 00 00 00 00 00 F0 00 00 00 00 00 BF
46, 37, 1, 4, 0 -> outside temp=0.1 is shown on vehicle info screen
2E410D0200000000000000A00000000000
46,65,2,1,32,0 = normal (closed, no park brake)
46,65,2,3,128,0 = low fuel

46, 32 : stereo
46, 33 : ac
46, 65, 2, 1  : doors etc
46, 65, 13 ,2  : vehicle status
46, 65, 2 ,3  : fuel,battery

```

### doors
```
46,65,2,
1,2,0
a b c

a = 1<>doors etc
b = bits...
(bonnet)0x20
(tailgate)0x10
(rr_door)0x08
(rl_door)0x04
(fr_door)0x02
(fl_door)0x01

c = chksum
```
### vehicle info
```
46, 65, 13, 
2,20,1,10,1,200,1,255,20,1,1,1,1,175
a  b c  d e  f  g  h   i j k l m n

a = 2<>vehicle info
b,c = tacho (rpm)
d,e = speed 
f,g = voltage
h,i = temperature
j,k,l = odometer
m = fuel_level
n = chksum
```

### fuel, battery
```
46,65,2,
3,2,0
a b c

a = 3<>fuel,battery
b = bits...
(low_fuel0x80;
(low_voltage)0x40;

c = chksum


### AC
```
- only shown at changes
46, 33, 5, 
32, 32, 1, 1, 0, 0
a   b   c  d  e  f

a = ac-type 
(powerfull)0x80
(ac)0x40
(recycling)0x20
(recycling_max)0x10
(recycling_min)0x08
(dual)0x04
(ac_max)0x02
(rear)0x01

b = where + speed(& 0x07)
(wind)0x80
(middle)0x40
(floor)0x20

c,d = temp left+right - 0=LO,1=18,2=18.5,17=26,18-30=NO,31=HI

e = extra
(aqs)0x80
(rear_lock)0x08
(ac_max) 0x04
(l_seat)(l_seat << 4) & 0x30
(r_seat)(r_seat) & 0x03

f = chksum
```


