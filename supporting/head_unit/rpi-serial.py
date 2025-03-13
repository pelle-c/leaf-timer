import serial
import time
import sys

# 2025 Per Carlén
# Faking serial messages to Android HU
# Settings in HU: Raise/VW/PQ/Low

def get_checksum(arr,len):
  sum = 0
  i = 1
  while i<len:
    sum = sum + arr[i]
    #print(arr[i])
    sum = sum & 0xff
    i += 1
  sum = (sum ^ 0xff)
  sum = sum & 0xff
  return sum


def test():
  print(get_checksum([46,238,2,89,22,160],5),160)
  print(get_checksum([46,196,1,10,48],4),48)
  print(get_checksum([46,192,8,7,48,0,0,0,0,0,0,0],8),0)
  exit(1)

ser = serial.Serial(
    port='/dev/ttyS0',
    baudrate=38400,
    parity=serial.PARITY_NONE,
    stopbits=serial.STOPBITS_TWO,
    bytesize=serial.EIGHTBITS
)


def get_rx_bytes():
  out=[]
  s = ""
  ts = 0
  while ser.inWaiting() > 0:
     b = ser.read(1)
     out.append(ord(b))
     if ord(b)>31 and ord(b)<128:
       s = s + chr(ord(b))
     if ts == 0:
       ts = time.time()
  if ts > 0:
    print(ts,out)
    print(ts,s)
    print("")


def rx():
  while True:
    get_rx_bytes()
    time.sleep(1)
 

def tx(arr):
  values = bytearray(arr)
  print("sending",str(values))
  print(arr)
  ser.write(values)

def tx_checksum(arr):
  c = get_checksum(arr,len(arr)-1)
  arr[len(arr)-1] = c
  tx(arr)

def ver():

  while True:
    arr = [46, 48, 17, 80, 69, 76, 76, 69, 45, 76, 69, 65, 70, 45, 86, 87, 80, 81, 45, 49 ,0]
    tx_checksum(arr)
    time.sleep(0.1)
    get_rx_bytes()
    time.sleep(1)

l = len(sys.argv)
print(sys.argv)
if l > 1:
  if sys.argv[l-1] == "-rx":
    rx()

  if sys.argv[l-1] == "-v":
    ver()

  if sys.argv[l-2] == "-s":
    arr = []
    items = sys.argv[l-1].split(",")
    for i, item in enumerate(items):
      arr.append(int(item))
    tx_checksum(arr)

  if sys.argv[l-2] == "-l":
    arr1 = []
    items = sys.argv[l-1].split(",")
    for i, item in enumerate(items):
      arr1.append(int(item))
    for x in range(0,255):
#      arr = [46,x,len(arr1)] + arr1 + [0]
      arr = [46,37,2,1,x,0]
#     arr = [46,65,2, 13,x,x,x,x,x,x,x,x,x,x,x,x,x,0]
#      arr = [46,65,2, 13,x,0,0,0,0,0,0,0,0,0,0,0,0,0]
      tx_checksum(arr)
#      arr = [46,65,3, 1,x,0]
#      tx_checksum(arr)
      time.sleep(0.1)
exit(0)
