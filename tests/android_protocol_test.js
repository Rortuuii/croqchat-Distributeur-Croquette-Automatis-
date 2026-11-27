// Vérifie le protocole de la page HTML avec des réponses fragmentées et des déconnexions simulées.
// Exerce les vraies fonctions du HTML, sans navigateur ni modification de l'app.
const fs = require('fs');
const vm = require('vm');
const assert = require('assert/strict');
const html = fs.readFileSync('Applicationde contrôle du distributeur.html', 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const transport = script.slice(script.indexOf('const SERVICE_UUID'), script.indexOf('const safeStorage'));
const receive = script.slice(script.indexOf('function onBleLine'), script.indexOf('async function sendCommand'));
const sandbox = {setTimeout, clearTimeout, TextEncoder, TextDecoder, Uint8Array, DataView, Promise,
  state:{showProtocol:false,meals:[]}, MAX_MEALS:6,
  renderMeals(){},renderStatus(){},saveLocal(){},toast(){},log(){},formatNumber:String};
vm.createContext(sandbox);
vm.runInContext(transport + receive + ';globalThis.FeederBLEClass=FeederBLE;',sandbox);
const ble = new sandbox.FeederBLEClass(sandbox.onBleLine,()=>{});
function notify(line) {
  const bytes=new TextEncoder().encode(line+'\n');
  for(let i=0;i<bytes.length;i+=20) {
    const part=bytes.slice(i,i+20);
    ble.onNotify({target:{value:new DataView(part.buffer)}});
  }
}
notify('MEALS_BEGIN');
notify('MEAL;id=1;time=08:00;mass=60;enable=1');
notify('MEAL;id=2;time=19:00;mass=55;enable=0');
notify('MEALS_END');
assert.equal(sandbox.state.meals.length,2);
assert.equal(sandbox.state.meals[1].enable,false);
notify('STATUS;w=100.5;busy=0;fault=0;sensor=1;cal=1;tare=1;clock=1;rfid=none;last=60');
assert.equal(sandbox.state.last,60); assert.equal(sandbox.state.clock,true);
notify('MEALS_BEGIN'); notify('MEAL;id=1;time=09:30;mass=70;enable=0'); notify('MEALS_END');
assert.equal(sandbox.state.meals.length,1);
assert.equal(sandbox.state.meals[0].mass,70);
notify('MEALS_BEGIN'); notify('MEALS_END'); assert.equal(sandbox.state.meals.length,0);
(async()=>{
  const packets=[];
  ble.device={gatt:{connected:true}};ble.tx={};
  ble.rx={writeValueWithResponse: async p => {assert.ok(p.length<=18); packets.push(new TextDecoder().decode(p));}};
  await ble.send('SET_MEAL;id=1;time=08:00;mass=60;enable=1');
  assert.equal(packets.join(''),'SET_MEAL;id=1;time=08:00;mass=60;enable=1\n');
  console.log('Android actual HTML: meals, status, RX/TX fragmentation PASS');
  // Les ACK de requetes de lecture ne terminent pas une lecture incomplete.
  const status=ble.request('GET_STATUS');
  await new Promise(resolve=>setImmediate(resolve));
  assert.equal(ble.pending.command,'GET_STATUS');
  notify('ACK;cmd=GET_STATUS');assert.ok(ble.pending);
  notify('STATUS;w=10;busy=0;sensor=1;cal=1;tare=1');await status;
  const tare=ble.request('TARE');
  await new Promise(resolve=>setImmediate(resolve));
  notify('ACK;cmd=TARE');assert.ok(ble.pending);
  notify('DONE;cmd=TARE');await tare;
  const calibration=ble.request('CALIBRATE;mass_ref=100');
  const rejected=assert.rejects(calibration,/INVALID_CALIBRATION/);
  await new Promise(resolve=>setImmediate(resolve));
  notify('ACK;cmd=CALIBRATE');assert.ok(ble.pending);
  notify('ERR;cmd=CALIBRATE;code=INVALID_CALIBRATION');await rejected;
  // STOP termine une ligne deja commencee avant d'envoyer sa propre ligne.
  packets.length=0;
  let release;
  ble.rx.writeValueWithResponse=async packet=>{
    packets.push(new TextDecoder().decode(packet));
    if(packets.length===1)await new Promise(resolve=>{release=resolve;});
  };
  const original=ble.request('SET_MEAL;id=1;time=08:00;mass=60;enable=1');
  const cancelled=assert.rejects(original,/STOP/);
  await new Promise(resolve=>setImmediate(resolve));
  const stop=ble.request('STOP');
  release();await cancelled;
  await new Promise(resolve=>setImmediate(resolve));
  assert.equal(packets.join(''),'SET_MEAL;id=1;time=08:00;mass=60;enable=1\nSTOP\n');
  notify('ACK;cmd=SET_MEAL');assert.equal(ble.pending.command,'STOP');
  notify('ACK;cmd=STOP');await stop;
  const lost=ble.request('GET_STATUS');const disconnected=assert.rejects(lost,/perdue/);
  await new Promise(resolve=>setImmediate(resolve));
  ble.tx={removeEventListener(){}};ble.onGATTDisconnected();await disconnected;
  console.log('BLE transactions: final replies/errors/priority STOP/disconnection PASS');
})().catch(e=>{console.error(e);process.exitCode=1;});
