#!/usr/bin/python3

import unittest
import IcedEspresso

url = '192.168.178.90'

class TestApi(unittest.TestCase):
    def test_invalid_address(self):
        iep = IcedEspresso.IcedEspresso(url)
        with self.assertRaises(IcedEspresso.HttpError) as cm:
            iep.post('bad_address',{})
        self.assertEqual(cm.exception.status_code, 404)

class TestStatusLed(unittest.TestCase):
    def test__bad_params(self):
        iep = IcedEspresso.IcedEspresso(url)
        with self.assertRaises(IcedEspresso.HttpError) as cm:
            iep.post('status_led',{'state':'test'})
        self.assertEqual(cm.exception.status_code, 400)

if __name__ == '__main__':
    unittest.main()
