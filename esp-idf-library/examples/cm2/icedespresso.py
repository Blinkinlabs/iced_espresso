#!/usr/bin/python3

import requests

class HttpError(Exception):
    def __init__(self, status_code, text):
        self.status_code = status_code
        self.text = text

        super().__init__(self.text)

    def __str__(self):
        return f'{self.status_code} -> {self.text}'

class IcedEspresso:
    def __init__(self, ip):
        self.base_url = 'http://{:}/'.format(ip)

    def get(self, address, params={}, data={}):
        response = requests.get(self.base_url + address, params, json = data)

        if (response.status_code != 200):
            raise HttpError(response.status_code, response.text)

        return response.json()

    def put(self, address, params={}, data={}):
        response = requests.put(self.base_url + address, params=params, json = data)

        if (response.status_code != 200):
            raise HttpError(response.status_code, response.text)

    def ota(self, image):
        response = requests.put(self.base_url + 'ota',
                data = image,
                headers={'Content-Type': 'application/octet-stream'})

        if (response.status_code != 200):
            raise HttpError(response.status_code, response.text)

    def status_led_get(self):
        return self.get('status_led')['state']

    def status_led_put(self, state):
        """ Set the status LED (true=on, false=off) """
        self.put('status_led', data={'state':state})

    def fpga_bitstream_write(self, bitstream):
        """ Write a bitstream to the FPGA, and start it """
        response = requests.put(self.base_url + 'fpga/bitstream',
                data = bitstream,
                headers={'Content-Type': 'application/octet-stream'})

        if (response.status_code != 200):
            raise HttpError(response.status_code, response.text)

    def register_read(self, address):
        return self.get('fpga_register', params={'address':address})

    def register_write(self, address, value):
        self.put('fpga_register', params={'address':address}, data={'value':value})

#    def write_memory(self, address, data):
#        self.put('fpga_register', params={'address':address}, data={'value':value})


    # CM-2 endpoints

    def rgb_led_put(self, r,g,b):
        """ Set the color of the status LED (r,g,b in range [0-1]) """
        self.put('rgb_led', data={'red':r, 'green':g, 'blue':b})

    def rgb_led_get(self):
        return self.get('rgb_led')

    def brightness_put(self, brightness):
        """ Set the display brightness (range [0-1]) """
        self.put('brightness', data={'brightness':brightness})

    def brightness_get(self):
        """ Set the display brightness (range [0-1]) """
        return self.get('brightness')['brightness']

    def bitmap_put(self, bitmap):
        response = requests.put(self.base_url + 'bitmap',
                data = bitmap,
                headers={'Content-Type': 'application/octet-stream'})

        if (response.status_code != 200):
            raise HttpError(response.status_code, response.text)
        

if __name__ == '__main__':
    import argparse
    import unittest

    class TestHttpApi(unittest.TestCase):
        def setUp(self):
            self.ie = IcedEspresso(self.ip)
        def test_status_led_get_put(self):
            self.ie.status_led_put(True)
            self.assertTrue(self.ie.status_led_get())

            self.ie.status_led_put(False)
            self.assertFalse(self.ie.status_led_get())

        def test_cm2_rgb_led_get_put(self):
            self.ie.rgb_led_put(0.1,0.2,0.3)
            ret = self.ie.rgb_led_get()
            self.assertAlmostEqual(ret['red'],0.1,places=4)
            self.assertAlmostEqual(ret['green'],0.2,places=4)
            self.assertAlmostEqual(ret['blue'],0.3,places=4)

            self.ie.rgb_led_put(0.9,0.8,0.7)
            ret = self.ie.rgb_led_get()
            self.assertAlmostEqual(ret['red'],0.9, places=4)
            self.assertAlmostEqual(ret['green'],0.8,places=4)
            self.assertAlmostEqual(ret['blue'],0.7,places=4)

        def test_cm2_brightness_get_put(self):
            self.ie.brightness_put(0.1)
            self.assertAlmostEqual(self.ie.brightness_get(), 0.1,places=4)

            self.ie.brightness_put(1)
            self.assertAlmostEqual(self.ie.brightness_get(),1,places=4)

        def test_cm2_bitmap_put(self):
            bitmap = bytearray(512)
            for i in range(0,len(bitmap)):
                bitmap[i] = 0
            bitmap[0] = 255
            self.ie.bitmap_put(bitmap)


    parser = argparse.ArgumentParser(description='ICEd ESPresso HTTP API unit test')
    parser.add_argument('-ip', required=True, help='IP address of ICEd ESPresso to test')

    args = parser.parse_args()
  
    TestHttpApi.ip = args.ip

    unittest.main(argv=[''])

    exit(0) 
    ie = IcedEspresso(args.ip)


    with open('fpga/top.bin', 'rb') as f:
        bitstream = f.read()
        ie.fpga_bitstream_write(bitstream)


    exit(0)

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
