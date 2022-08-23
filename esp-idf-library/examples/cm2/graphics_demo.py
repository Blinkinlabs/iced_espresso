import icedespresso
import argparse
import time

from PIL import Image, ImageFont, ImageDraw

parser = argparse.ArgumentParser(description='ICEd ESPresso HTTP API unit test')
parser.add_argument('-ip', required=True, help='IP address of ICEd ESPresso to test')

args = parser.parse_args()

ie = icedespresso.IcedEspresso(args.ip)

image = Image.new('L',[16,32])
draw = ImageDraw.Draw(image)

x = 0
while True:
    draw.rectangle((0,0,15,31),fill=0)

    draw.line((x,0,15-x,31), fill=int(x/16.0*50))

    draw.text((-x,16),'test',fill=255)

    ie.bitmap_put(image.tobytes())

    x += 1
    if x > 15:
        x = 0

    time.sleep(.1)
