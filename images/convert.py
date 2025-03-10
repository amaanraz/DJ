from PIL import Image
import numpy
import sys
import os

if len(sys.argv) != 2:
    print('Usage: python3 convert.py <input_image_dir>')
    exit()

input_image = sys.argv[1]
filename = os.path.splitext(os.path.basename(input_image))[0]
output_file = filename + '.data'

img = Image.open(input_image)
pix = img.load()
w, h = img.size

byte_array = numpy.zeros(w*h, dtype=numpy.int32)
index = 0
for i in range(0, h):
    for j in range(0, w):
        red, green, blue = pix[j, i]
        red_4bit = int(red/16)
        green_4bit = int(green/16)
        blue_4bit = int(blue/16)
        byte_array[index] = (blue_4bit << 20) | (green_4bit << 12) | (red_4bit << 4)
        index = index + 1

byte_array = bytes(byte_array)


with open(output_file, "wb") as binary_file:
    binary_file.write(byte_array)