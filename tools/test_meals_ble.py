"""Verification BLE reelle : uniquement un repas DESACTIVE, jamais de moteur.

python tools/test_meals_ble.py [--reboot-port COM3]
Dependances : bleak ; pyserial si verification apres redemarrage.
Le slot libre est supprime a la fin ; aucune configuration existante n'est remplacee.
"""
import argparse
import asyncio
import time

from bleak import BleakClient, BleakScanner

RX = '32a44c10-fdd0-4b30-bb4b-5a8900000002'
TX = '32a44c10-fdd0-4b30-bb4b-5a8900000003'


class Link:
    def __init__(self):
        self.buffer = bytearray()
        self.lines = asyncio.Queue()
        self.client = None

    def notify(self, _sender, data):
        self.buffer.extend(data)
        while b'\n' in self.buffer:
            line, _, remainder = self.buffer.partition(b'\n')
            self.buffer = bytearray(remainder)
            self.lines.put_nowait(line.decode('ascii').rstrip('\r'))

    async def connect(self):
        for attempt in range(3):
            try:
                device = await BleakScanner.find_device_by_filter(
                    lambda d, a: (a.local_name or d.name or '') == 'CroqChat-ESP32', timeout=15)
                if not device:
                    raise RuntimeError('CroqChat-ESP32 absent du scan Bluetooth')
                # Une carte redemarree invalide les objets GATT mis en cache par Windows.
                self.client = BleakClient(device, winrt={'use_cached_services': False}, timeout=20)
                await self.client.connect()
                await self.client.start_notify(TX, self.notify)
                await asyncio.sleep(0.3)
                return
            except Exception:
                await self.close()
                if attempt == 2:
                    raise
                print('Reconnexion Bluetooth Windows...', flush=True)
                await asyncio.sleep(2)

    async def command(self, text, final=None):
        print('TX', text, flush=True)
        encoded = (text + '\n').encode('ascii')
        for start in range(0, len(encoded), 18):
            await self.client.write_gatt_char(RX, encoded[start:start + 18], response=True)
        result = []
        while True:
            line = await asyncio.wait_for(self.lines.get(), timeout=10)
            print('RX', line, flush=True)
            result.append(line)
            if line.startswith('ERR;'):
                raise RuntimeError(line)
            if (final and line.startswith(final)) or (not final and line == 'ACK;cmd=' + text.split(';')[0]):
                return result

    async def close(self):
        if self.client and self.client.is_connected:
            try:
                await self.client.stop_notify(TX)
            except (OSError, RuntimeError):
                pass  # Peut ne pas avoir ete active, notamment apres une erreur GATT.
            finally:
                await self.client.disconnect()


def fields(line):
    return dict(part.split('=', 1) for part in line.split(';')[1:])


async def main(reboot_port):
    link = Link()
    slot = None
    try:
        await link.connect()
        initial = await link.command('GET_MEALS', 'MEALS_END')
        meals = [fields(x) for x in initial if x.startswith('MEAL;')]
        used = {int(x['id']) for x in meals}
        slot = next((i for i in range(6, 0, -1) if i not in used), None)
        if slot is None:
            raise RuntimeError('Six slots occupes : test annule sans modifier le planning')
        await link.command(f'SET_MEAL;id={slot};time=08:00;mass=60;enable=0')
        lines = await link.command('GET_MEALS', 'MEALS_END')
        assert f'MEAL;id={slot};time=08:00;mass=60;enable=0' in lines
        await link.command(f'SET_MEAL;id={slot};time=09:30;mass=75;enable=0')
        expected = f'MEAL;id={slot};time=09:30;mass=75;enable=0'
        assert expected in await link.command('GET_MEALS', 'MEALS_END')
        if reboot_port:
            import serial
            await link.close()
            await asyncio.sleep(1)
            with serial.Serial(reboot_port, 115200, timeout=0.5) as port:
                port.dtr = False
                port.rts = True
                await asyncio.sleep(0.15)
                port.rts = False
                await asyncio.sleep(3)
            link = Link()
            await link.connect()
            assert expected in await link.command('GET_MEALS', 'MEALS_END')
            status = await link.command('GET_STATUS', 'STATUS;')
            assert fields(status[-1])['clock'] == '0'
        # Ne synchroniser que si aucun repas existant n'est actif.
        if not any(x['enable'] == '1' for x in meals):
            offset = int(time.localtime().tm_gmtoff // 60) if hasattr(time.localtime(), 'tm_gmtoff') else int(
                -time.altzone // 60 if time.localtime().tm_isdst else -time.timezone // 60)
            await link.command(f'SET_TIME;epoch={int(time.time())};offset={offset}')
            status = await link.command('GET_STATUS', 'STATUS;')
            assert fields(status[-1])['clock'] == '1'
        await link.command(f'DELETE_MEAL;id={slot}')
        final = await link.command('GET_MEALS', 'MEALS_END')
        assert not any(x.startswith(f'MEAL;id={slot};') for x in final)
        assert sorted(x for x in initial if x.startswith('MEAL;')) == sorted(x for x in final if x.startswith('MEAL;'))
        slot = None
        print('BLE hardware meals create/edit/read/delete/status: PASS', flush=True)
    finally:
        if slot is not None and link.client and link.client.is_connected:
            await link.command(f'DELETE_MEAL;id={slot}')
        await link.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--reboot-port')
    asyncio.run(main(parser.parse_args().reboot_port))
