import aiohttp
import asyncio
import time
import random

async def main():

    async with aiohttp.ClientSession() as session:

        ips= ['192.168.178.91',
            '192.168.178.92',
            '192.168.178.50',
            '192.168.178.93']
        for ip in ips:
            url = 'http://' + ip + '/bitmap'

            bitmap = bytearray(256)
            for i in range(0, 256):
                bitmap[i] = random.randint(0,160)

            async with session.post(url, data=bitmap) as resp:
                pokemon = await resp.text()
                print(pokemon)

start_time = time.time()
asyncio.run(main()) 
print("--- %s seconds ---" % (time.time() - start_time))
