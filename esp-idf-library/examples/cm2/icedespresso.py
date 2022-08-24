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

    def get(self, address, params={}):
        response = requests.get(self.base_url + address, params)

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

    def fpga_bitstream_put(self, bitstream):
        """ Write a bitstream to the FPGA, and start it """
        response = requests.put(self.base_url + 'fpga/bitstream',
                data = bitstream,
                headers={'Content-Type': 'application/octet-stream'})

        if (response.status_code != 200):
            raise HttpError(response.status_code, response.text)

    def register_get(self, address):
        return self.get('fpga/register', params={'address':address})['value']

    def register_put(self, address, value):
        self.put('fpga/register', params={'address':address}, data={'value':value})

    def memory_get(self, address, length):
        response = requests.get(self.base_url + 'fpga/memory',
                params={'address':address, 'length':length}
                )

        if (response.status_code != 200):
            raise HttpError(response.status_code, response.text)

        # TODO: what does this look like?
        return response.content

    def memory_put(self, address, data):
        response = requests.put(self.base_url + 'fpga/memory',
                params={'address':address},
                data = data,
                headers={'Content-Type': 'application/octet-stream'}
                )

        if (response.status_code != 200):
            raise HttpError(response.status_code, response.text)


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

    unittest.TestLoader.sortTestMethodsUsing = None

    class TestHttpApi(unittest.TestCase):
        def setUp(self):
            self.ie = IcedEspresso(self.ip)

        def test_status_led_put_bad_data(self):
            with self.assertRaises(HttpError):
                self.ie.put('status_led')

            with self.assertRaises(HttpError):
                self.ie.status_led_put(-1)

            with self.assertRaises(HttpError):
                self.ie.status_led_put('')

        def test_status_led_get_put(self):
            self.ie.status_led_put(True)
            self.assertTrue(self.ie.status_led_get())

            self.ie.status_led_put(False)
            self.assertFalse(self.ie.status_led_get())

        def skip_test_0_fpga_bitstream_put(self):
            with open('fpga/top.bin', 'rb') as f:
                bitstream = f.read()
                self.ie.fpga_bitstream_put(bitstream)

        def test_fpga_register_get_bad_addr(self):
            with self.assertRaises(HttpError):
                self.ie.register_get('string')

            with self.assertRaises(HttpError):
                self.ie.register_get(-1)

            with self.assertRaises(HttpError):
                self.ie.register_get(0x10000)

        def test_fpga_register_put_bad_addr(self):
            with self.assertRaises(HttpError):
                self.ie.put('fpga/register', data={'value':0})

            with self.assertRaises(HttpError):
                self.ie.register_put('string',0)

            with self.assertRaises(HttpError):
                self.ie.register_put(-1,0)

            with self.assertRaises(HttpError):
                self.ie.register_put(0x10000,0)

        def test_fpga_register_put_bad_data(self):
            with self.assertRaises(HttpError):
                self.ie.put('fpga/register', params={'address':0})

            with self.assertRaises(HttpError):
                self.ie.register_put(0,'string')

            with self.assertRaises(HttpError):
                self.ie.register_put(0,-1)

            with self.assertRaises(HttpError):
                self.ie.register_put(0,0x10000)

        def test_fpga_register_get_put(self):
            address_a = 0x00F0 # RGB led 'red' value
            address_b = 0x00F1 # RGB led 'green' value
            self.ie.register_put(address_a, 0xAAAA)
            self.ie.register_put(address_b, 0x5555)
            self.assertEqual(self.ie.register_get(address_a),0xAAAA)
            self.assertEqual(self.ie.register_get(address_b),0x5555)

            self.ie.register_put(address_a, 0xFFFF)
            self.ie.register_put(address_b, 0x0000)
            self.assertEqual(self.ie.register_get(address_a),0xFFFF)
            self.assertEqual(self.ie.register_get(address_b),0x0000)

        def test_fpga_memory_get_bad_addr(self):
            with self.assertRaises(HttpError):
                self.ie.memory_get('string',1)

            with self.assertRaises(HttpError):
                self.ie.memory_get(-1,1)

            with self.assertRaises(HttpError):
                self.ie.memory_get(0x10000,1)

        def test_fpga_memory_get_bad_length(self):
            with self.assertRaises(HttpError):
                self.ie.memory_get(0,'string')

            with self.assertRaises(HttpError):
                self.ie.memory_get(0,0)

            with self.assertRaises(HttpError):
                self.ie.memory_get(0,513)

        def test_fpga_memory_put_bad_addr(self):
            with self.assertRaises(HttpError):
                self.ie.memory_put('string',bytearray(1))

            with self.assertRaises(HttpError):
                self.ie.memory_put(-1,bytearray(1))

            with self.assertRaises(HttpError):
                self.ie.memory_put(0x10000,bytearray(1))

        def test_fpga_memory_put_bad_data(self):
            # TODO
            #with self.assertRaises(HttpError):
            #    self.ie.put('fpga/memory', params={'address':0})

            with self.assertRaises(HttpError):
                self.ie.memory_put(0, bytearray(513))

        def test_fpga_memory_get(self):
            response = self.ie.memory_get(0,1)
            self.assertEqual(len(response),1)

            response = self.ie.memory_get(0,255)
            self.assertEqual(len(response),255)
        
        # TODO
        def skip_test_fpga_memory_put_get(self):
            address_a = 0x0000 # left led panel buffer
            address_b = 0x0200 # right led panel buffer

            data_a = bytearray(512)
            for i in range(0,len(data_a)):
                data_a[i] = i % 256

            data_b = bytearray(512)
            for i in range(0,len(data_b)):
                data_b[i] = (255-i) % 256

            self.assertNotEqual(data_a, data_b)

            self.ie.memory_put(address_a, data_a)
            self.ie.memory_put(address_b, data_b)
            response_a = self.ie.memory_get(address_a, len(data_a))
            response_b = self.ie.memory_get(address_b, len(data_b))
            self.assertEqual(data_a, response_a)
            self.assertEqual(data_b, response_b)

            self.ie.memory_put(address_a, data_b)
            self.ie.memory_put(address_b, data_a)
            response_a = self.ie.memory_get(address_a, len(data_b))
            response_b = self.ie.memory_get(address_b, len(data_a))
            self.assertEqual(data_b, response_a)
            self.assertEqual(data_a, response_b)

        def test_cm2_rgb_led_put_baddata(self):
            with self.assertRaises(HttpError):
                self.ie.put('rgb_led')

            bad_min = -0.0000001
            bad_max = 1.0000001

            with self.assertRaises(HttpError):
                self.ie.rgb_led_put(bad_min,0,0)
            with self.assertRaises(HttpError):
                self.ie.rgb_led_put(bad_max,0,0)

            with self.assertRaises(HttpError):
                self.ie.rgb_led_put(0,bad_min,0)
            with self.assertRaises(HttpError):
                self.ie.rgb_led_put(0,bad_max,0)

            with self.assertRaises(HttpError):
                self.ie.rgb_led_put(0,0,bad_min)
            with self.assertRaises(HttpError):
                self.ie.rgb_led_put(0,0,bad_max)

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

        def test_cm2_brightness_put_bad_data(self):
            with self.assertRaises(HttpError):
                self.ie.put('brightness')

            with self.assertRaises(HttpError):
                self.ie.brightness_put(-0.0000001)

            with self.assertRaises(HttpError):
                self.ie.brightness_put(1.0000001)

            with self.assertRaises(HttpError):
                self.ie.brightness_put('string')

        def test_cm2_brightness_get_put(self):
            self.ie.brightness_put(0.1)
            self.assertAlmostEqual(self.ie.brightness_get(), 0.1,places=4)

            self.ie.brightness_put(1)
            self.assertAlmostEqual(self.ie.brightness_get(),1,places=4)

        def test_cm2_bitmap_put_baddata(self):
            with self.assertRaises(HttpError):
                self.ie.put('bitmap')

            with self.assertRaises(HttpError):
                self.ie.bitmap_put(bytearray(0))

            with self.assertRaises(HttpError):
                self.ie.bitmap_put(bytearray(511))

            with self.assertRaises(HttpError):
                self.ie.bitmap_put(bytearray(513))

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

#    ie = IcedEspresso(args.ip)
#    address_a = 0x0000 # left led panel buffer
#
#    data_a = bytearray(512)
#    for i in range(0,len(data_a)):
#        data_a[i] = i % 256
#
#    ie.memory_put(address_a, data_a)
#    response_a = ie.memory_get(address_a, 10)
#    print(len(response_a), ['{:02x}'.format(i) for i in response_a])
