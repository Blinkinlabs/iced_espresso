#!/usr/bin/python3

import icedespresso
import struct

def bitflip(val):
    b = 0
    for i in range(8):
        b <<= 1
        b |= val >> i & 1
    return b

class WS2822(icedespresso.IcedEspresso):
    power_reg = 0x0000
    channel_count_reg = 0x0001
    data_mode_reg = 0x0002
    send_dmx_reg = 0x0003

    dmx_addr = 0x0000

    def set_power(self, state):
        if state:
            self.register_put(self.power_reg, 0x0001)
        else:
            self.register_put(self.power_reg, 0x0000)

    def get_power(self):
        return (self.register_get(self.power_reg) & 0x0001) == 1

    def set_data_mode(self, data, address):
        self.register_put(self.data_mode_reg, (0x0001 if data else 0x0000) | (0x0002 if address else 0x000))

    def set_channel_count(self, count):
        self.register_put(self.channel_count_reg, count)

    def send_dmx(self, channels):
        import requests
        response = requests.put(self.base_url + 'dmx',
                data = channels,
                headers={'Content-Type': 'application/octet-stream'})

        self.register_put(self.send_dmx_reg, 0x0001)

        while self.register_get(self.send_dmx_reg) == 0x0001:
            pass

    def program_address(self, channel):
        # pull address high and data low to start program
        ws2822.set_data_mode(False, False)
        ws2822.set_power(False)
        time.sleep(1)
        ws2822.set_power(True)
        ws2822.set_data_mode(False, True)
        time.sleep(.2)

        # transition address low for 1s, to enter programming mode
        ws2822.set_data_mode(False, False)
        time.sleep(1)
        ws2822.set_data_mode(False, True)

        # Construct and send address programming message
        channels = bytearray()
        channels.append(bitflip((channel)%256))
        channels.append(bitflip(240 - (channel >> 8)*15))
        channels.append(bitflip(0xD2))
        channels.append(0)  # 16-bit aligned

        ws2822.set_channel_count(3)
        ws2822.send_dmx(channels)

        time.sleep(.1)
    
        # pull address low and data low? to end program
        ws2822.set_data_mode(True, True)
        time.sleep(.3)      # wait to show white light
    
        ws2822.set_data_mode(False, False)
        ws2822.set_power(False)
        time.sleep(.3)
        ws2822.set_power(True)
        ws2822.set_data_mode(True, False)


if __name__ == '__main__':
    import time
    import argparse

    parser = argparse.ArgumentParser(description='Program WS2821/WS2822 addresses')
    parser.add_argument('--ip', type=str, default='172.16.1.125', help='IP address of Iced Espresso')
    parser.add_argument('number', type=int, help='Number to program. DMX address = Number*3')
    args = parser.parse_args()

    #ip = '172.16.1.125'

    ws2822 = WS2822(args.ip)

    channel = 1+3*(args.number-1)

    print('Programming tile to:', args.number, 'The tile will flash white on successful program, then cycle r,g,b three times')

    # Send programming data
    ws2822.program_address(channel)

    # send color data
    ws2822.set_channel_count(3*170)

    for i in range(0,3):
        data = bytearray()
        for i in range(0,170):
            if i*3 + 1 == channel:
                data.append(255)
                data.append(0)
                data.append(0)
            else:
                data.append(0)
                data.append(0)
                data.append(0)
        ws2822.send_dmx(data)
        time.sleep(.1)

        data = bytearray()
        for i in range(0,170):
            if i*3 + 1 == channel:
                data.append(0)
                data.append(255)
                data.append(0)
            else:
                data.append(0)
                data.append(0)
                data.append(0)
        ws2822.send_dmx(data)
        time.sleep(.1)

        data = bytearray()
        for i in range(0,170):
            if i*3 + 1 == channel:
                data.append(0)
                data.append(0)
                data.append(255)
            else:
                data.append(0)
                data.append(0)
                data.append(0)
        ws2822.send_dmx(data)
        time.sleep(.1)

    ws2822.set_power(False)
