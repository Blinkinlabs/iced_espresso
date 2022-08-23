#!/usr/bin/python3

import requests
import argparse

class HttpError(Exception):
    def __init__(self, status_code, text):
        self.status_code = status_code
        self.text = text

        super().__init__(self.text)

    def __str__(self):
        return f'{self.status_code} -> {self.text}'

class IcedEspresso:
    def __init__(self, ip):
        self.base_url = 'http://' + ip + '/'

    def put(self, address, data):
        response = requests.put(self.base_url + address, json = data)

        if (response.status_code != 200):
            raise HttpError(response.status_code, response.text)

    def get(self, address, data):
        response = requests.get(self.base_url + address, json = data)

        if (response.status_code != 200):
            raise HttpError(response.status_code, response.text)

        return response.json()

    def set_rgb_led(self, r,g,b):
        """ Set the color of the status LED (r,g,b in range [0-1]) """
        self.put('rgb_led', {'red':r, 'green':g, 'blue':b})

    def set_status_led(self, state):
        """ Set the status LED (true=on, false=off) """
        self.put('status_led', {'state':state})

    def write_register(self, address, value):
        self.put('fpga_register', {'address':address, 'value':value})

    def read_register(self, address):
        return self.get('fpga_register', {'address':address})

    def write_memory(self, address, data):
        self.put('fpga_register', {'address':address, 'value':value})

    def ota(self, image):
        response = requests.put(self.base_url + 'ota',
                data = image,
                headers={'Content-Type': 'application/octet-stream'})

        if (response.status_code != 200):
            raise HttpError(response.status_code, response.text)

    def write_bitmap(self, image):
        response = requests.put(self.base_url + 'bitmap',
                data = image,
                headers={'Content-Type': 'application/octet-stream'})

        if (response.status_code != 200):
            raise HttpError(response.status_code, response.text)
        

if __name__ == '__main__':
    import time
    import random
   
    ies = [
        IcedEspresso('172.16.1.184'),
        ]

    for ie in ies:
        ie.set_rgb_led(0,0,0)
        ie.set_status_led(False)

    while True:
        bitmap = bytearray(256)
        for i in range(0, 256):
            bitmap[i] = random.randint(0,255)
#            if random.randint(0,100) > 25:
#                bitmap[i] = random.randint(0,255)
#            else:
#                bitmap[i] = 0

        for ie in ies:
            #ie.put('brightness', {'brightness':random.randint(0,160)})
            ie.write_bitmap(bitmap)

        time.sleep(.05)
    exit(0)

    ie.write_register(0x00F0, 65535)   # red
    ie.write_register(0x00F1, 0)   # green
    ie.write_register(0x00F2, 0)   # blue



    status = True
    while True:
        ie.set_rgb_led(1,1,0)
        ie.set_status_led(status)
        status = not status
        time.sleep(1)

        ie.set_rgb_led(0,1,1)
        ie.set_status_led(status)
        status = not status
        time.sleep(1)

        ie.set_rgb_led(1,0,1)
        ie.set_status_led(status)
        status = not status
        time.sleep(1)
