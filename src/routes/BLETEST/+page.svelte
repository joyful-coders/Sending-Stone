<script lang="ts">
  const SERVICE_UUID = 'db118277-ac3c-4312-9c3f-8f0f77e70acc';
  const CHAR_UUID    = '40d3f957-dded-4b7d-9eb2-f11db97dda09';

  let status = $state<'idle' | 'connecting' | 'connected' | 'error'>('idle');
  let error = $state('');
  let data = $state({ temp: 0, humidity: 0, battery: 0, ts: 0 });

  let device: BluetoothDevice | null = null;

  function onNotify(e: Event) {
    const dv = (e.target as BluetoothRemoteGATTCharacteristic).value!;
  const bytes = Array.from(new Uint8Array(dv.buffer, dv.byteOffset, dv.byteLength));
  console.log('notify', dv.byteLength, bytes);

    data = {
      temp: dv.getInt16(0, true) / 100,
      humidity: dv.getUint16(2, true) / 100,
      battery: dv.getUint16(4, true),
      ts: Date.now()
    };
  }

  async function connect() {
    try {
      status = 'connecting';
    //   device = await navigator.bluetooth.requestDevice({
    //     filters: [{ services: [SERVICE_UUID] }]
    //   });
    device = await navigator.bluetooth.requestDevice({
  acceptAllDevices: true,
  optionalServices: [SERVICE_UUID]   // required, or getPrimaryService will fail later
});
      device.addEventListener('gattserverdisconnected', () => (status = 'idle'));

    const server  = await device.gatt!.connect();            console.log('connected');
    const service = await server.getPrimaryService('db118277-ac3c-4312-9c3f-8f0f77e70acc');

for (const c of await service.getCharacteristics()) {
  console.log(c.uuid, c.properties);
  if (c.properties.notify) {
    c.addEventListener('characteristicvaluechanged', (e) => {
      const dv = (e.target as BluetoothRemoteGATTCharacteristic).value!;
      console.log(c.uuid, dv.byteLength, Array.from(new Uint8Array(dv.buffer, dv.byteOffset, dv.byteLength)));
    });
    await c.startNotifications();
  }
}
    const char    = await service.getCharacteristic(CHAR_UUID);   console.log('char ok', char.properties);
    await char.startNotifications();                         console.log('notify ok');

    char.addEventListener('characteristicvaluechanged', onNotify);
      await char.startNotifications();
      
      status = 'connected';
    } catch (err) {
      error = (err as Error).message;
      status = 'error';
    }
  }

  function disconnect() {
    device?.gatt?.disconnect();
  }
</script>

{#if status !== 'connected'}
  <button onclick={connect} disabled={status === 'connecting'}>
    {status === 'connecting' ? 'Connecting…' : 'Connect to ESP32'}
  </button>
{:else}
  <button onclick={disconnect}>Disconnect</button>
  <p>Temp: {data.temp.toFixed(2)} °C</p>
  <p>Humidity: {data.humidity.toFixed(1)} %</p>
  <p>Battery: {data.battery} mV</p>
{/if}

{#if error}<p style="color:red">{error}</p>{/if}