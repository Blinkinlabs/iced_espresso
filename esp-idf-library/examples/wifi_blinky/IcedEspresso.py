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
        pass

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
        self.post('rgb_led', {'red':r, 'green':g, 'blue':b})

    def set_status_led(self, state):
        """ Set the status LED (true=on, false=off) """
        self.put('status_led', {'state':state})

    def register_write(self, address, value):
        self.put('fpga/register', {'address':address, 'value':value})

    def register_read(self, address):
        return self.get('fpga/register', {'address':address})

    def ota(self, image):
        response = requests.put(self.base_url + 'ota',
                data = image,
                headers={'Content-Type': 'application/octet-stream'})

        if (response.status_code != 200):
            raise HttpError(response.status_code, response.text)
        

if __name__ == '__main__':
    import time
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument('ip')
    args = parser.parse_args()

    iep = IcedEspresso(args.ip)

    iep.register_write(0x00F0, 65535)   # red
    iep.register_write(0x00F1, 0)   # green
    iep.register_write(0x00F2, 0)   # blue

    with open('build/wifi-blinky.bin', 'rb') as f:
        update_image = f.read()

        iep.ota(update_image)
        exit(0)


    status = True
    while True:
        iep.set_rgb_led(1,1,0)
        iep.set_status_led(status)
        status = not status
        time.sleep(1)

        iep.set_rgb_led(0,1,1)
        iep.set_status_led(status)
        status = not status
        time.sleep(1)

        iep.set_rgb_led(1,0,1)
        iep.set_status_led(status)
        status = not status
        time.sleep(1)
