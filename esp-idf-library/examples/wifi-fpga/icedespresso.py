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

    def get(self, address, data = None):
        # TODO: drop the data argument
        if data == None:
            data = {}

        response = requests.get(self.base_url + address, json = data)

        if (response.status_code != 200):
            raise HttpError(response.status_code, response.text)

        return response.json()

    def status_led_put(self, state):
        """ Set the status LED (true=on, false=off) """
        self.put('status_led', {'state':state})

    def status_led_get(self):
        """ Gets the status LED state (true=on, false=off) """
        return self.get('status_led')['state']

    def register_put(self, address, value):
        #self.put('fpga/register/{}'.format(address), {'value':value})
        self.put('fpga/register', {'address':address, 'value':value})

    def register_get(self, address):
        #return self.get('fpga/register/{}'.format(address))
        return self.get('fpga/register', {'address':address})['value']

    def memory_put(self, address, data):
        #self.put('fpga/memory/{}'.format(address), {'value':data})
        self.put('fpga/memory', {'address':address, 'value':data})

    def ota(self, image):
        response = requests.put(self.base_url + 'ota',
                data = image,
                headers={'Content-Type': 'application/octet-stream'})

        if (response.status_code != 200):
            raise HttpError(response.status_code, response.text)


if __name__ == '__main__':
    import unittest

    ip = '172.16.1.185'

    class TestRestAPI(unittest.TestCase):
        def setUp(self):
            self.dut = IcedEspresso(ip)

        def test_status_led_put_bad_val(self):
            with self.assertRaises(HttpError) as ctx:
                self.dut.status_led_put('bad')
            self.assertEqual(ctx.exception.status_code, 400)

        def test_status_led_put(self):
            self.dut.status_led_put(False)
            self.dut.status_led_put(True)

        def test_status_led_get(self):
            self.dut.status_led_put(False)
            self.assertFalse(self.dut.status_led_get())
            self.dut.status_led_put(True)
            self.assertTrue(self.dut.status_led_get())

        def test_register_put_badaddress(self):
            with self.assertRaises(HttpError) as ctx:
                self.dut.register_put(-1, 0)
            self.assertEqual(ctx.exception.status_code, 400)

            with self.assertRaises(HttpError) as ctx:
                self.dut.register_put(65536, 0)
            self.assertEqual(ctx.exception.status_code, 400)

        def test_register_put_baddata(self):
            with self.assertRaises(HttpError) as ctx:
                self.dut.register_put(0, 'test')
            self.assertEqual(ctx.exception.status_code, 400)

            with self.assertRaises(HttpError) as ctx:
                self.dut.register_put(0, -1)
            self.assertEqual(ctx.exception.status_code, 400)

            with self.assertRaises(HttpError) as ctx:
                self.dut.register_put(0, 65536)
            self.assertEqual(ctx.exception.status_code, 400)

        def test_register_get_badaddress(self):
            with self.assertRaises(HttpError) as ctx:
                self.dut.register_get(-1)
            self.assertEqual(ctx.exception.status_code, 400)

            with self.assertRaises(HttpError) as ctx:
                self.dut.register_get(65536)
            self.assertEqual(ctx.exception.status_code, 400)

        def test_register_putget(self):
            # Note: FPGA gateware must implement this register
            address = 0x00F0
            self.dut.register_put(address, 0x1234)
            self.assertEqual(self.dut.register_get(address),0x1234)
            self.dut.register_put(address, 0x4321)
            self.assertEqual(self.dut.register_get(address),0x4321)


    unittest.main()
